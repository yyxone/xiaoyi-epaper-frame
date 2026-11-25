"""Check the embedded page against a local synthetic ESP32 receiver with Playwright."""
from argparse import ArgumentParser
from hashlib import sha256
from io import BytesIO
from pathlib import Path
import re
from urllib.parse import parse_qs, urlsplit

from PIL import Image
from playwright.sync_api import expect, sync_playwright
import panel


def check_desktop_inks(page):
    # Neutral exposure isolates color quantization, including near-black and
    # clipped highlights meeting in the middle and along the row boundaries.
    image = Image.new("RGB", (800, 480))
    image.putdata([((x+3*y)%256, (2*x+y)%256, (x^y)%256)
                   for y in range(480) for x in range(800)])
    source = BytesIO()
    image.save(source, "PNG")
    source.seek(0)
    expected = sha256(panel.prepare(source, mode="crop")).hexdigest()
    actual = page.evaluate("""() => {
      const pixels=new Uint8ClampedArray(WIDTH*HEIGHT*4);
      for (let y=0;y<HEIGHT;y++) for (let x=0;x<WIDTH;x++) {
        const i=(y*WIDTH+x)*4;
        pixels[i]=(x+3*y)%256; pixels[i+1]=(2*x+y)%256;
        pixels[i+2]=(x^y)%256; pixels[i+3]=255;
      }
      return sha256(quantizePhoto(pixels,new Float32Array(256).fill(1)));
    }""")
    assert actual == expected, (actual, expected)


def png(name, size, color, blocks=()):
    stream = BytesIO()
    image = Image.new("RGB", size, color)
    for box, fill in blocks:
        image.paste(fill, box)
    image.save(stream, format="PNG")
    return {"name": name, "mimeType": "image/png", "buffer": stream.getvalue()}


def check_print_tones(page):
    result = page.evaluate("""() => {
      const levels=[0,16,32,48,128,208,224,240,255];
      const source=new Uint8ClampedArray(WIDTH*HEIGHT*4);
      // Horizontal bands isolate the diffusion boundaries from the measurements.
      const band=Math.floor(HEIGHT/levels.length);
      for (let y=0;y<HEIGHT;y++) {
        const value=levels[Math.min(levels.length-1,Math.floor(y/band))];
        for (let x=0;x<WIDTH;x++) {
          const i=4*(y*WIDTH+x);
          source[i]=source[i+1]=source[i+2]=value; source[i+3]=255;
        }
      }
      function means(mode) {
        const frame=diffuse(source.slice(),mode);
        return levels.map((_,j)=>{
          let sum=0, count=0;
          for (let y=j*band+12;y<j*band+40;y++) for (let x=20;x<WIDTH-20;x++) {
            const i=y*WIDTH+x, byte=frame[i>>1];
            const [r,g,b]=palette[i%2?byte&15:byte>>4];
            sum+=(77*r+150*g+29*b)/256; count++;
          }
          return sum/count;
        });
      }
      return {detail:means("detail"),classic:means("classic")};
    }""")
    detail, classic = result["detail"], result["classic"]
    assert all(b-a > 0.8 for a,b in zip(detail, detail[1:])), result
    assert detail[3]-detail[0] > classic[3]-classic[0]+6, result
    assert detail[-1]-detail[5] > classic[-1]-classic[5]+6, result
    assert max(abs(detail[i]-classic[i]) for i in (0,4,8)) < 5, result
    return result


def check_scan_direction(page):
    # Check left-to-right brightness on each row parity and measure the
    # imbalance between the two diagonals inside each constant-gray patch.
    bands = page.evaluate("""() => {
      const source=new Uint8ClampedArray(WIDTH*HEIGHT*4);
      for(let y=0;y<HEIGHT;y++) for(let x=0;x<WIDTH;x++) {
        const i=(y*WIDTH+x)*4, gray=17*Math.floor(x/50);
        source[i]=source[i+1]=source[i+2]=gray; source[i+3]=255;
      }
      const frame=diffuse(source,'detail');
      const luma=palette.map(([r,g,b])=>(77*r+150*g+29*b)/256);
      function value(x,y) {
        const byte=frame[(y*WIDTH+x)>>1];
        return luma[x%2 ? byte&15 : byte>>4];
      }
      return Array.from({length:16},(_,band)=>{
        const sums=[0,0], diagonal=[0,0];
        for(let y=10;y<HEIGHT-10;y++) for(let x=band*50+8;x<band*50+41;x++) {
          sums[y%2]+=value(x,y);
          diagonal[0]+=Math.abs(value(x,y)-value(x+1,y+1));
          diagonal[1]+=Math.abs(value(x+1,y)-value(x,y+1));
        }
        const count=(HEIGHT-20)*33/2;
        return [sums[0]/count,sums[1]/count,
          Math.abs(diagonal[0]-diagonal[1])/Math.max(1,diagonal[0]+diagonal[1])];
      });
    }""")
    for parity in (0, 1):
        assert all(right[parity] > left[parity] + 1 for left, right in zip(bands, bands[1:])), bands
    # More permissive than v11's measured ~0.003, but catches v10's ~0.09.
    assert sum(b[2] for b in bands[1:-1]) / 14 < .035, bands


def check(browser_path, engine):
    header = Path(__file__).parent / "firmware/simple_epaper/family_page.h"
    html = header.read_text(encoding="utf-8").split('R"familyweb(', 1)[1].rsplit(')familyweb"', 1)[0]
    origin = "http://family-screen.test:28463"
    state = dict(boot_id="1" * 16, job=0, batch=0, state="idle", sha256="", album_sha="",
                 web_job="", batch_request="",
                 source="manual", slides=0, interval_minutes=30, next_slide=0, next_ms=0, wait_ms=0, refresh_ms=0,
                 photo_ready=True, album_ready=True, revision="1" * 16 + "-0",
                 shown_sha="", shown_slot=-1, shown_current=False, shown_size=0,
                 storage_ready=True, storage_error=False)
    album = []
    opened, sent, errors = [], [], []
    # Midtones distinguish both rendering profiles even after a narrow mobile crop.
    photo_a = png("portrait.png", (2000, 3000), "#dc756a", [((600, 0, 1700, 3000), "#87ab89"), ((0, 1450, 2000, 3000), "#557cae")])
    photo_b = png("landscape.png", (320, 180), "blue")
    photo_c = png("third.png", (320, 180), "green")
    photo_d = png("new.png", (320, 180), "yellow")
    photo_e = png("extra.png", (320, 180), "black")

    def bump_revision():
        state["batch"] += 1
        state["revision"] = f"{state['boot_id']}-{state['batch']}"

    def mock_board(route):
        request = route.request
        url = urlsplit(request.url)
        if url.scheme == "blob":
            route.continue_()
            return
        path = url.path
        opened.append(path)
        if path == "/":
            csp = "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src blob:; worker-src blob:; connect-src 'self'; frame-ancestors 'none'"
            route.fulfill(content_type="text/html; charset=utf-8", body=html, headers={"Content-Security-Policy": csp})
            return
        assert "x-family-code" not in request.headers
        if path == "/web/status":
            route.fulfill(json=state)
            return
        if path == "/web/shown-preview":
            assert parse_qs(url.query)["sha"] == [state["shown_sha"]]
            route.fulfill(content_type="application/octet-stream", body=b"\x33" * 192_000)
            return
        if path == "/web/album-previews":
            assert parse_qs(url.query)["revision"] == [state["revision"]]
            body = b"".join(frame[:1] * 46_080 for frame in album)
            route.fulfill(content_type="application/octet-stream", body=body)
            return
        assert request.all_headers().get("origin") == origin, (request.method, path, request.all_headers().get("origin"))
        if path == "/web/resume":
            assert request.method == "POST" and request.headers["x-album-revision"] == state["revision"]
            state.update(paused=False, state="idle")
            route.fulfill(json=state)
            return
        if path == "/web/arrange":
            assert request.method == "POST" and request.headers["x-album-revision"] == state["revision"]
            slots = list(map(int, request.headers["x-album-slots"].split(",")))
            assert len(slots) >= 2 and len(set(slots)) == len(slots)
            album[:] = [album[i] for i in slots]
            bump_revision()
            state.update(batch_request=request.headers["x-web-request"], slides=len(album),
                         interval_minutes=int(request.headers["x-rotation-minutes"]), album_sha="", shown_slot=-1)
            route.abort()  # The board saved the edit, but its response was lost.
            return
        if path == "/web/stop":
            assert request.method == "POST"
            assert request.headers["x-album-revision"] == state["revision"]
            album.clear()
            bump_revision()
            state.update(slides=0, next_ms=0, album_sha="", shown_slot=-1)
            route.fulfill(json=state)
            return
        payload = request.post_data_buffer
        assert request.method == "PUT"
        assert request.headers["content-type"] == "application/octet-stream"
        digest = sha256(payload).hexdigest()
        assert request.headers["x-frame-sha256"] == digest
        request_id = request.headers["x-web-request"]
        assert re.fullmatch(r"[0-9a-f]{16}", request_id)
        assert all(value >> 4 <= 5 and value & 15 <= 5 for value in payload)
        sent.append((path, payload))
        if path == "/web/frame":
            assert request.headers["x-album-revision"] == state["revision"]
            assert len(payload) == 192_000
            state.update(job=state["job"] + 1, web_job=request_id, state="displayed", sha256=digest,
                         refresh_ms=28122, shown_sha=digest, shown_current=True, shown_size=192_000)
            # Simulate a lost submission response. Readback must reconcile without another PUT.
            route.abort()
        else:
            assert request.headers["x-album-revision"] == state["revision"]
            frames = [payload[i:i + 192_000] for i in range(0, len(payload), 192_000)]
            assert all(len(frame) == 192_000 for frame in frames)
            if path == "/web/replace":
                slots = [int(index) for index in request.headers["x-album-slots"].split(",")]
                assert slots == sorted(set(slots)) and len(slots) == len(frames) <= 2
                for slot, frame in zip(slots, frames):
                    album[slot] = frame
                if state["shown_slot"] in slots:
                    state["shown_slot"] = -1
            else:
                assert path == "/web/carousel" and 2 <= len(frames) <= 12
                assert request.headers["x-rotation-minutes"] == "30"
                album[:] = frames
                state["shown_slot"] = 0 if sha256(frames[0]).hexdigest() == state["shown_sha"] else -1
            bump_revision()
            state.update(batch_request=request_id, album_sha=digest, slides=len(album), next_ms=180_000)
            route.fulfill(status=202, json=state)

    with sync_playwright() as playwright:
        browser = getattr(playwright, engine).launch(executable_path=browser_path, headless=True)
        page = browser.new_page(viewport={"width": 390, "height": 820}, is_mobile=True, has_touch=True)
        page.add_init_script("""window.work = {decodes:0, conversions:0};
        const decode = HTMLImageElement.prototype.decode;
        HTMLImageElement.prototype.decode = function () { ++work.decodes; return decode.call(this); };
        const post = Worker.prototype.postMessage;
        Worker.prototype.postMessage = function (...args) { ++work.conversions; return post.apply(this,args); };""")
        page.on("pageerror", lambda error: errors.append(str(error)))
        dismiss_next = []
        page.on("dialog", lambda dialog: dialog.dismiss() if dismiss_next and dismiss_next.pop() else dialog.accept())
        page.route("**/*", mock_board)
        page.goto(origin + "/")
        expect(page.locator("#status")).to_contain_text("墨水屏在线")
        check_desktop_inks(page)
        check_print_tones(page)
        check_scan_direction(page)
        assert not page.evaluate("() => window.isSecureContext")
        expect(page.locator('input[type="password"]')).to_have_count(0)
        assert page.evaluate("() => sha256(new TextEncoder().encode('abc'))") == sha256(b"abc").hexdigest()
        assert page.evaluate("() => document.documentElement.scrollWidth <= innerWidth")
        photos = page.locator("#photos")
        photos.set_input_files([photo_a, photo_b, photo_c])
        expect(page.locator("#send")).to_be_enabled()
        canvas = page.locator("#preview")
        canvas.scroll_into_view_if_needed()
        original = canvas.evaluate("element => element.toDataURL()")
        bounds = canvas.bounding_box()
        x = round(bounds["x"] + bounds["width"] / 2)
        y = round(bounds["y"] + bounds["height"] / 2)
        if engine == "chromium":
            touch = page.context.new_cdp_session(page)
            def fingers(event, distance=0):
                points = [{"x": x - distance, "y": y, "id": 1}, {"x": x + distance, "y": y, "id": 2}] if distance else []
                touch.send("Input.dispatchTouchEvent", {"type": event, "touchPoints": points})
            fingers("touchStart", 35)
            fingers("touchMove", 85)
            fingers("touchEnd")
            assert float(page.locator("#zoom").input_value()) > 2
            page.mouse.move(x, y)
            before_wheel = float(page.locator("#zoom").input_value())
            page.mouse.wheel(0, -90)
            assert float(page.locator("#zoom").input_value()) > before_wheel
        page.locator("#zoom").evaluate("input => { input.value = 2; input.dispatchEvent(new Event('input')); }")
        page.evaluate("() => new Promise(requestAnimationFrame)")
        zoomed = canvas.evaluate("element => element.toDataURL()")
        assert zoomed != original
        page.mouse.move(x, y)
        page.mouse.down()
        page.mouse.move(x + 55, y + 20, steps=16)
        page.mouse.up()
        page.evaluate("() => new Promise(requestAnimationFrame)")
        cropped = canvas.evaluate("element => element.toDataURL()")
        assert cropped != zoomed
        assert page.evaluate("() => work") == {"decodes": 1, "conversions": 0}
        assert not sent
        # Whole-photo framing must leave the user's previous full-bleed crop intact.
        framing = page.locator("#framing")
        framing.select_option("fit")
        expect(page.locator("#zoomTools")).to_be_hidden()
        page.evaluate("() => new Promise(requestAnimationFrame)")
        assert page.evaluate("previewCanvas.getContext('2d').getImageData(0,0,1,1).data.slice(0,3).every(c=>c===255)")
        framing.select_option("fill")
        page.evaluate("() => new Promise(requestAnimationFrame)")
        assert canvas.evaluate("el=>el.toDataURL()") == cropped
        page.locator("#zoomIn").click()
        expect(page.locator("#zoom")).to_have_value("2.1")
        page.locator("#zoomOut").click()
        expect(page.locator("#zoom")).to_have_value("2")
        page.locator("#draftTools summary").click()
        page.locator("#later").click()
        page.locator("#earlier").click()
        page.locator("#draftJump").select_option("2")
        expect(page.locator("#photoLabel")).to_contain_text("third.png")
        page.locator("#append").set_input_files(photo_d)
        expect(page.locator("#photoLabel")).to_contain_text("new.png")
        page.locator("#removeDraft").click()
        page.locator("#draftJump").select_option("0")
        expect(page.locator("#zoom")).to_have_value("2")
        page.evaluate("() => new Promise(requestAnimationFrame)")
        assert canvas.evaluate("el=>el.toDataURL()") == cropped
        style = page.locator("#inkMode")
        expect(style).to_have_value("detail")
        style.select_option("classic")
        expect(page.locator("#proof")).to_have_text("返回裁剪")
        classic = canvas.evaluate("el=>el.toDataURL()")
        style.select_option("detail")
        expect(page.locator("#proof")).to_have_text("返回裁剪")
        assert classic != canvas.evaluate("el=>el.toDataURL()")
        style.select_option("classic")
        expect(page.locator("#proof")).to_have_text("返回裁剪")
        page.locator("#next").click()
        expect(style).to_have_value("detail")
        expect(page.locator("#zoom")).to_have_value("1")
        page.locator("#prev").click()
        expect(style).to_have_value("classic")
        expect(page.locator("#zoom")).to_have_value("2")
        page.evaluate("() => new Promise(requestAnimationFrame)")
        assert canvas.evaluate("element => element.toDataURL()") == cropped
        page.locator("#proof").click()
        expect(page.locator("#proof")).to_have_text("返回裁剪")
        proof_hash = canvas.evaluate("""element => {
          const rgba=element.getContext('2d').getImageData(0,0,800,480).data;
          const colors=new Map(palette.map(([r,g,b],index)=>[(r<<16)|(g<<8)|b,index]));
          const packed=new Uint8Array(192000);
          for(let i=0;i<384000;i++) {
            const offset=i*4;
            const color=colors.get((rgba[offset]<<16)|(rgba[offset+1]<<8)|rgba[offset+2]);
            if (color===undefined) throw Error('Preview color absent from palette');
            packed[i>>1]|=i%2 ? color : color<<4;
          }
          return sha256(packed);
        }""")
        page.locator("#send").click()
        expect(page.locator("#notice")).to_contain_text("确认显示这张照片")
        assert proof_hash == sha256(sent[0][1]).hexdigest()
        expect(page.locator("#displayThumb")).to_be_visible()
        assert page.locator("#displayThumb").get_attribute("width") == "800"
        page.locator("#enlarge").click()
        expect(page.locator("#lightbox")).to_be_visible()
        assert page.locator("#largeCanvas").get_attribute("width") == "800"
        page.locator("#closeLarge").click()
        page.locator("#start").click()
        expect(page.locator("#notice")).to_contain_text("轮播已启动")
        expect(page.locator("#albumSummary")).to_contain_text("共 3 张")
        expect(page.locator("#album .slide")).to_have_count(3)
        assert page.locator("#album canvas").first.get_attribute("width") == "160"
        expect(page.locator("#album .playing").first).to_have_text("正在显示")

        # The board reports a failed hardware refresh. Recovery never uploads new photos.
        before_failure = album[:]
        state.update(paused=True, state="failed")
        page.evaluate("readStatus()")
        expect(page.locator("#status")).to_contain_text("照片已保留")
        expect(page.locator("#timing")).to_be_hidden()
        page.locator("#resume").click()
        expect(page.locator("#albumNotice")).to_contain_text("轮播已恢复")
        assert album == before_failure and len(sent) == 2
        first, second, third = album[:]
        assert first == sent[0][1]
        page.locator("#album input").nth(1).check()
        with page.expect_file_chooser() as popup:
            page.locator("#replace").click()
        popup.value.set_files([photo_d])
        expect(page.locator("#notice")).to_contain_text("调整新照片")
        assert len(sent) == 2
        page.locator("#start").click()
        expect(page.locator("#notice")).to_contain_text("所选照片已替换")
        expect(page.locator("#album .slide")).to_have_count(3)
        assert album[0] == first and album[1] != second and album[2] == third

        middle = album[1]
        page.locator("#album input").nth(0).check()
        page.locator("#album input").nth(2).check()
        with page.expect_file_chooser() as popup:
            page.locator("#replace").click()
        popup.value.set_files([photo_e])
        expect(page.locator("#albumNotice")).to_contain_text("选择 2 张")
        assert len(sent) == 3
        with page.expect_file_chooser() as popup:
            page.locator("#replace").click()
        popup.value.set_files([photo_e, photo_d])
        expect(page.locator("#albumNotice")).to_be_empty()
        expect(page.locator("#notice")).to_contain_text("调整新照片")
        assert len(sent) == 3
        page.locator("#swapReplacement").click()
        expect(page.locator("#photoLabel")).to_contain_text("替换屏上第 3 张")
        expected_replacements = page.evaluate("async()=>Promise.all(photos.map(async(item)=>sha256(await prepareItem(item,null))))")
        page.locator("#start").click()
        expect(page.locator("#notice")).to_contain_text("所选照片已替换")
        expect(page.locator("#album .slide")).to_have_count(3)
        assert album[0] == middle and album[1] == middle and album[2] != third
        assert expected_replacements == [sha256(album[0]).hexdigest(), sha256(album[2]).hexdigest()]

        # A cancelled destructive action leaves the live album intact.
        dismiss_next.append(True)
        page.locator("#send").click()
        assert not dismiss_next and len(sent) == 4 and len(album) == 3
        page.locator("#organize").click()
        page.locator("#orderList [data-move=down]").first.click()
        page.locator("#cancelLayout").click()
        assert len(album) == 3 and "/web/arrange" not in opened
        # Another family member changes the list while the dialog is still open.
        page.locator("#organize").click()
        bump_revision()
        page.evaluate("readStatus()")
        expect(page.locator("#layout")).to_be_hidden()
        expect(page.locator("#albumNotice")).to_contain_text("按最新列表重新整理")
        # Reorder, remove and change timing with a single metadata request.
        page.locator("#organize").click()
        expect(page.locator("#layout")).to_be_visible()
        page.locator("#orderList [data-move=down]").first.click()
        page.locator("#orderList [data-move=remove]").last.click()
        page.locator("#layoutInterval").select_option("15")
        before_layout = album[:]
        page.locator("#saveLayout").click()
        expect(page.locator("#albumNotice")).to_contain_text("设置已保存")
        assert album == [before_layout[1], before_layout[0]] and state["interval_minutes"] == 15
        assert len(sent) == 4
        photos.set_input_files([photo_b, photo_c])
        expect(page.locator("#start")).to_be_enabled()
        page.locator("#start").click()
        expect(page.locator("#albumSummary")).to_contain_text("共 2 张")
        page.locator("#stop").click()
        expect(page.locator("#notice")).to_contain_text("轮播列表已清空")
        expect(page.locator("#album .slide")).to_have_count(0)
        expect(page.locator("#displayThumb")).to_be_visible()
        photos.set_input_files([dict(photo_a, name=f"photo-{i}.png") for i in range(13)])
        expect(page.locator("#notice")).to_contain_text("之前的选择和裁剪已保留")
        expect(page.locator("#send")).to_be_enabled()
        assert [path for path, _ in sent] == ["/web/frame", "/web/carousel", "/web/replace", "/web/replace", "/web/carousel"]
        assert set(opened) <= {"/", "/web/status", "/web/frame", "/web/carousel", "/web/replace", "/web/stop",
                               "/web/shown-preview", "/web/album-previews", "/web/arrange", "/web/resume"}
        # Recover from a missed connection without resetting this browser's draft.
        offline = lambda route: route.abort()
        page.route("**/web/status", offline)
        page.evaluate("readStatus()")
        expect(page.locator("#retry")).to_be_visible()
        expect(page.locator("#send")).to_be_disabled()
        page.unroute("**/web/status", offline)
        page.locator("#retry").click()
        expect(page.locator("#send")).to_be_enabled()
        expect(page.locator("#photoLabel")).to_contain_text("landscape.png")
        page.set_viewport_size({"width": 320, "height": 640})
        assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
        assert not errors, errors
        old_link = browser.new_page()
        old_link.route("**/*", mock_board)
        old_link.goto(origin + "/#k=" + "1" * 32)
        expect(old_link.locator("#status")).to_contain_text("墨水屏在线")
        assert "#" not in old_link.url
        browser.close()


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--edge")
    parser.add_argument("--engine", choices=["chromium", "webkit"], default="chromium")
    args = parser.parse_args()
    check(args.edge, args.engine)
    print(f"PASS {args.engine}: drag, zoom, no conversions during gestures, crop retained, preview equals payload, gallery edits, clear, and lost receipts")
