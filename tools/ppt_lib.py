# -*- coding: utf-8 -*-
"""排版工具库：学术极简深蓝主题，供 make_video_ppt.py 调用。

坐标单位统一为英寸（inch），画布 13.333 x 7.5。
所有形状默认无阴影、无描边，避免 PowerPoint 默认样式干扰。
"""
import io
import os

from PIL import Image, ImageFilter
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, MSO_AUTO_SIZE, PP_ALIGN
from pptx.oxml.ns import qn
from pptx.util import Emu, Inches, Pt

# ---------------------------------------------------------------- 颜色
BG      = RGBColor(0x0B, 0x16, 0x22)   # 深海军蓝底
PANEL   = RGBColor(0x12, 0x25, 0x38)   # 卡片
PANEL_2 = RGBColor(0x18, 0x2F, 0x46)   # 卡片高亮
LINE    = RGBColor(0x27, 0x42, 0x59)   # 分隔线
TEXT    = RGBColor(0xE9, 0xF0, 0xF7)   # 主文字
MUTED   = RGBColor(0x9A, 0xAF, 0xC2)   # 次要文字
DIM     = RGBColor(0x6A, 0x81, 0x97)   # 弱文字
ACCENT  = RGBColor(0x4D, 0xA3, 0xD8)   # 钢蓝强调
ACCENT2 = RGBColor(0x8C, 0xC9, 0xE8)   # 浅钢蓝
AMBER   = RGBColor(0xD8, 0xA9, 0x57)   # 数字/提醒
RED     = RGBColor(0xD9, 0x5C, 0x5C)   # 标注框
GREEN   = RGBColor(0x63, 0xA9, 0x83)   # 通过
WHITE   = RGBColor(0xFF, 0xFF, 0xFF)

SANS = "Segoe UI"
EA   = "微软雅黑"
MONO = "Consolas"

# ---------------------------------------------------------------- 基础形状


def _no_shadow(shape):
    try:
        shape.shadow.inherit = False
    except Exception:
        pass


def rect(slide, x, y, w, h, fill=PANEL, line=None, radius=None, lw=0.75):
    """矩形卡片。radius 给定时用圆角。"""
    shp_type = MSO_SHAPE.ROUNDED_RECTANGLE if radius else MSO_SHAPE.RECTANGLE
    s = slide.shapes.add_shape(shp_type, Inches(x), Inches(y), Inches(w), Inches(h))
    if radius:
        try:
            s.adjustments[0] = radius / min(w, h) if min(w, h) else 0.1
        except Exception:
            pass
    if fill is None:
        s.fill.background()
    else:
        s.fill.solid()
        s.fill.fore_color.rgb = fill
    if line is None:
        s.line.fill.background()
    else:
        s.line.color.rgb = line
        s.line.width = Pt(lw)
    _no_shadow(s)
    s.text_frame.word_wrap = True
    s.text_frame.margin_left = 0
    s.text_frame.margin_right = 0
    s.text_frame.margin_top = 0
    s.text_frame.margin_bottom = 0
    return s


def hline(slide, x, y, w, color=LINE, lw=0.75):
    s = slide.shapes.add_connector(1, Inches(x), Inches(y), Inches(x + w), Inches(y))
    s.line.color.rgb = color
    s.line.width = Pt(lw)
    return s


def vline(slide, x, y, h, color=LINE, lw=0.75):
    s = slide.shapes.add_connector(1, Inches(x), Inches(y), Inches(x), Inches(y + h))
    s.line.color.rgb = color
    s.line.width = Pt(lw)
    return s


def arrow(slide, x, y, w=0.16, h=0.16, color=DIM, direction="right"):
    m = {"right": MSO_SHAPE.RIGHT_ARROW, "down": MSO_SHAPE.DOWN_ARROW,
         "left": MSO_SHAPE.LEFT_ARROW, "up": MSO_SHAPE.UP_ARROW}
    s = slide.shapes.add_shape(m[direction], Inches(x), Inches(y), Inches(w), Inches(h))
    s.fill.solid()
    s.fill.fore_color.rgb = color
    s.line.fill.background()
    _no_shadow(s)
    return s


def chip(slide, x, y, w, h, label, size=9, color=MUTED, fill=PANEL_2,
         line=LINE, bold=False, mono=False, radius=0.06):
    shp = rect(slide, x, y, w, h, fill=fill, line=line, radius=radius)
    text(slide, x, y, w, h, [label], size=size, color=color, bold=bold,
         mono=mono, align="c", anchor="m")
    return shp


# ---------------------------------------------------------------- 文本


def _set_ea(run, name):
    rPr = run._r.get_or_add_rPr()
    ea = rPr.find(qn("a:ea"))
    if ea is None:
        ea = rPr.makeelement(qn("a:ea"), {})
        latin = rPr.find(qn("a:latin"))
        if latin is not None:
            latin.addnext(ea)
        else:
            rPr.append(ea)
    ea.set("typeface", name)


def _letter_spacing(run, val):
    """val 单位 1/100 pt。"""
    run._r.get_or_add_rPr().set("spc", str(int(val)))


def _apply(run, size=11, color=TEXT, bold=False, italic=False,
           font=SANS, mono=False, spacing=None, underline=False):
    f = run.font
    f.size = Pt(size)
    f.bold = bold
    f.italic = italic
    f.underline = underline
    name = MONO if mono else font
    f.name = name
    _set_ea(run, name)
    run.font.color.rgb = color
    if spacing:
        _letter_spacing(run, spacing)


ALIGN = {"l": PP_ALIGN.LEFT, "c": PP_ALIGN.CENTER, "r": PP_ALIGN.RIGHT,
         "j": PP_ALIGN.JUSTIFY}
ANCHOR = {"t": MSO_ANCHOR.TOP, "m": MSO_ANCHOR.MIDDLE, "b": MSO_ANCHOR.BOTTOM}


def _norm(p):
    """把段落规格归一成 dict。"""
    if isinstance(p, str):
        return {"runs": [{"text": p}]}
    if isinstance(p, (list, tuple)):
        return {"runs": [{"text": t} if isinstance(t, str) else t for t in p]}
    d = dict(p)
    if "text" in d and "runs" not in d:
        d["runs"] = [{"text": d.pop("text")}]
    return d


def text(slide, x, y, w, h, paras, size=11, color=TEXT, bold=False,
         font=SANS, mono=False, align="l", anchor="t", line_spacing=None,
         space_after=0, margins=(0, 0, 0, 0), wrap=True, spacing=None):
    """多段落文本。paras 内每项可为 str / dict / list[run]。"""
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame
    tf.word_wrap = wrap
    tf.auto_size = MSO_AUTO_SIZE.NONE
    tf.vertical_anchor = ANCHOR[anchor]
    tf.margin_left, tf.margin_right, tf.margin_top, tf.margin_bottom = [Inches(v) for v in margins]

    if isinstance(paras, (str, dict)):
        paras = [paras]

    for i, raw in enumerate(paras):
        p = _norm(raw)
        para = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        para.alignment = ALIGN[p.get("align", align)]
        para.space_before = Pt(p.get("space_before", 0))
        para.space_after = Pt(p.get("space_after", space_after))
        ls = p.get("line_spacing", line_spacing)
        if ls:
            para.line_spacing = ls
        psize = p.get("size", size)
        for r in p["runs"]:
            run = para.add_run()
            run.text = r.get("text", "")
            _apply(run, size=r.get("size", psize),
                   color=r.get("color", p.get("color", color)),
                   bold=r.get("bold", p.get("bold", bold)),
                   italic=r.get("italic", False),
                   font=r.get("font", p.get("font", font)),
                   mono=r.get("mono", p.get("mono", mono)),
                   spacing=r.get("spacing", p.get("spacing", spacing)),
                   underline=r.get("underline", False))
    return box


# ---------------------------------------------------------------- 页头页脚


def header(slide, tag, title, page_no, total=16, sub=None):
    # 左侧竖线 + 章节标签
    rect(slide, 0.62, 0.40, 0.045, 0.30, fill=ACCENT)
    text(slide, 0.78, 0.38, 8.0, 0.24, [tag], size=9, color=ACCENT,
         bold=True, spacing=120)
    text(slide, 0.62, 0.70, 11.4, 0.55, [title], size=25, color=TEXT, bold=True)
    if sub:
        text(slide, 0.62, 1.22, 11.4, 0.24, [sub], size=9.5, color=DIM)
        hline(slide, 0.62, 1.46, 12.09, LINE, 0.75)
        _footer(slide, page_no, total, y=7.02)
        return 1.58
    hline(slide, 0.62, 1.33, 12.09, LINE, 0.75)
    _footer(slide, page_no, total, y=7.02)
    return 1.46


def _footer(slide, page_no, total, y=7.02, right="编译原理课程设计 · 期末汇报"):
    text(slide, 0.62, y, 1.2, 0.24, [[{"text": f"{page_no:02d}", "color": ACCENT, "bold": True, "mono": True, "size": 9.5},
                                      {"text": f"  / {total:02d}", "color": DIM, "mono": True, "size": 9.5}]])
    text(slide, 8.0, y, 4.71, 0.24, [right], size=8.5, color=DIM, align="r", spacing=60)


def section_label(slide, x, y, w, label, color=ACCENT):
    text(slide, x, y, w, 0.22, [label], size=9, color=color, bold=True, spacing=100)


# ---------------------------------------------------------------- 图片

CROP_DEFAULT = (0.003, 0.026, 0.003, 0.0)   # 去掉窗口左右细边与标题栏


def load_img(path, crop=CROP_DEFAULT, max_scale=None, up=1):
    """裁剪后转 PNG 流，返回 (stream, w_px, h_px, crop)。

    up>1 时先做 LANCZOS 上采样再轻微锐化。截图被裁到内容区后像素往往不够
    （铺满 13.3 英寸宽时只有 50~70 dpi），直接交给 PowerPoint 拉伸会发虚；
    用 LANCZOS + UnsharpMask 预先插值，边缘比放映时的快速缩放干净得多。
    """
    im = Image.open(path).convert("RGB")
    W, H = im.size
    if crop:
        l, t, r, b = crop
        im = im.crop((int(l * W), int(t * H), int(W - r * W), int(H - b * H)))
    if max_scale:
        im = im.resize((int(im.width * max_scale), int(im.height * max_scale)), Image.LANCZOS)
    elif up and up != 1:
        im = im.resize((im.width * up, im.height * up), Image.LANCZOS)
        im = im.filter(ImageFilter.UnsharpMask(radius=1.1, percent=85, threshold=3))
    buf = io.BytesIO()
    im.save(buf, "PNG")
    buf.seek(0)
    return buf, im.width, im.height


def place(slide, src, box, border=True, border_color=LINE):
    """把 (stream,w,h) 或路径按 contain 方式放进 box=(x,y,w,h)，居中，返回实际矩形。"""
    x, y, bw, bh = box
    if isinstance(src, tuple):
        stream, iw, ih = src[0], src[1], src[2]
    else:
        stream, iw, ih = load_img(src)
    scale = min(bw / iw, bh / ih)
    w, h = iw * scale, ih * scale
    left, top = x + (bw - w) / 2, y + (bh - h) / 2
    pic = slide.shapes.add_picture(stream, Inches(left), Inches(top), Inches(w), Inches(h))
    if border:
        pic.line.color.rgb = border_color
        pic.line.width = Pt(0.75)
    return (left, top, w, h)


def place_fill(slide, src, box, crop_hint=0.5):
    """按 fill 方式放进 box（裁剪多余部分），用于封面纹理。"""
    x, y, bw, bh = box
    stream, iw, ih = (src[0], src[1], src[2]) if isinstance(src, tuple) else load_img(src)[:3]
    tr, br = bw / bh, iw / ih
    if tr > br:                      # 需要更高：上下裁
        h, w = bw / br, bw
    else:
        w, h = bh * br, bh
    pic = slide.shapes.add_picture(stream, Inches(x + (bw - w) / 2), Inches(y + (bh - h) / 2),
                                   Inches(w), Inches(h))
    return (x, y, bw, bh)


def annotate(slide, rect_, crop, fx, fy, fw, fh, color=RED, lw=1.5, radius=0.04,
             label=None, label_pos=None, label_size=8.5):
    """在图片上按原图百分比画红框。rect_ 为 place() 返回的实际矩形。"""
    l, t, w, h = rect_
    cl, ct, cr, cb = crop or (0, 0, 0, 0)
    sx = (fx - cl) / (1 - cl - cr)
    sy = (fy - ct) / (1 - ct - cb)
    sw = fw / (1 - cl - cr)
    sh = fh / (1 - ct - cb)
    bx = l + sx * w
    by = t + sy * h
    bw_ = sw * w
    bh_ = sh * h
    s = rect(slide, bx, by, bw_, bh_, fill=None, line=color, lw=lw, radius=radius)
    if label:
        lx, ly = label_pos if label_pos else (bx + bw_ + 0.06, by - 0.10)
        text(slide, lx, ly, 2.4, 0.2, [label], size=label_size, color=color, bold=True)
    return (bx, by, bw_, bh_)


def leader(slide, pts, color=ACCENT, lw=0.9, dash=None):
    """折线引导线，pts 为 [(x,y), ...]。"""
    for i in range(len(pts) - 1):
        c = slide.shapes.add_connector(1, Inches(pts[i][0]), Inches(pts[i][1]),
                                       Inches(pts[i + 1][0]), Inches(pts[i + 1][1]))
        c.line.color.rgb = color
        c.line.width = Pt(lw)
        if dash:
            c.line.dash_style = dash


def badge(slide, x, y, label, size=9, fill=ACCENT, color=BG, w=None, h=0.26, bold=True):
    w = w or (0.11 * len(label) + 0.24)
    s = rect(slide, x, y, w, h, fill=fill, line=None, radius=h / 2)
    text(slide, x, y, w, h, [label], size=size, color=color, bold=bold, align="c", anchor="m")
    return s


def stamp(slide, cx, cy, r, top, bottom, color=GREEN):
    """圆形印章。"""
    s = slide.shapes.add_shape(MSO_SHAPE.OVAL, Inches(cx - r), Inches(cy - r),
                               Inches(2 * r), Inches(2 * r))
    s.fill.background()
    s.line.color.rgb = color
    s.line.width = Pt(1.25)
    _no_shadow(s)
    text(slide, cx - r, cy - r + 0.06, 2 * r, 2 * r - 0.12,
         [[{"text": top, "size": 13, "bold": True, "color": color, "mono": True}],
          [{"text": bottom, "size": 6.5, "color": color, "spacing": 60}]],
         align="c", anchor="m")


# ---------------------------------------------------------------- 表格


def set_border(cell, color=LINE, width=0.75, sides=("b",)):
    tcPr = cell._tc.get_or_add_tcPr()
    tag_map = {"l": "a:lnL", "r": "a:lnR", "t": "a:lnT", "b": "a:lnB"}
    for s in sides:
        tag = tag_map[s]
        ln = tcPr.find(qn(tag))
        if ln is not None:
            tcPr.remove(ln)
        ln = tcPr.makeelement(qn(tag), {"w": str(int(width * 12700)), "cap": "flat",
                                        "cmpd": "sng", "algn": "ctr"})
        fill = ln.makeelement(qn("a:solidFill"), {})
        clr = ln.makeelement(qn("a:srgbClr"), {"val": str(color)})
        fill.append(clr)
        ln.append(fill)
        # 顺序：lnL, lnR, lnT, lnB 需在 tcPr 起始处
        tcPr.insert(0, ln)


def table(slide, x, y, w, col_w, rows, header_row=True, row_h=0.42, head_h=0.42,
          size=10.5, head_size=9.5, head_color=MUTED, color=TEXT,
          zebra=True, mono_cols=(), align=None, first_col_color=None):
    """rows: 二维列表，元素可为 str 或 (str, dict)。"""
    nrow, ncol = len(rows), len(rows[0])
    gt = slide.shapes.add_table(nrow, ncol, Inches(x), Inches(y), Inches(w), Inches(head_h + row_h * (nrow - 1)))
    tbl = gt.table
    tbl.first_row = False
    tbl.horz_banding = False
    for i, cw in enumerate(col_w):
        tbl.columns[i].width = Inches(cw)
    for i in range(nrow):
        tbl.rows[i].height = Inches(head_h if (i == 0 and header_row) else row_h)

    for ri, row in enumerate(rows):
        for ci, val in enumerate(row):
            cell = tbl.cell(ri, ci)
            cell.margin_left = Inches(0.12)
            cell.margin_right = Inches(0.1)
            cell.margin_top = Inches(0.02)
            cell.margin_bottom = Inches(0.02)
            cell.vertical_anchor = MSO_ANCHOR.MIDDLE
            cell.fill.solid()
            if ri == 0 and header_row:
                cell.fill.fore_color.rgb = PANEL_2
            elif zebra and ri % 2 == 0:
                cell.fill.fore_color.rgb = PANEL
            else:
                cell.fill.fore_color.rgb = BG
            spec = val if isinstance(val, tuple) else (val, {})
            txt, opt = spec[0], spec[1]
            is_head = ri == 0 and header_row
            a = (align[ci] if align else "l")
            tf = cell.text_frame
            tf.word_wrap = True
            p = tf.paragraphs[0]
            p.alignment = ALIGN[a]
            run = p.add_run()
            run.text = str(txt)
            _apply(run,
                   size=opt.get("size", head_size if is_head else size),
                   color=opt.get("color", head_color if is_head else (first_col_color or color) if ci == 0 else color),
                   bold=opt.get("bold", is_head),
                   mono=opt.get("mono", ci in mono_cols))
            set_border(cell, LINE if not is_head else LINE,
                       0.75 if not is_head else 1.0, sides=("b",))
    return gt
