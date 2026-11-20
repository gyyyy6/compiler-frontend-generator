# -*- coding: utf-8 -*-
"""按 docs/VIDEO_SCRIPT.md 生成 23 页视频讲解 PPT（+1 页隐藏提词卡附录）。

图文分离：文字页只放要点，截图单独占整页（占画面 80%~93%，按内容区裁剪后
有效字号放大约 2~3 倍）；每页备注写入时长、金句、画面提示与逐字稿。

产物：CompilePrinciple_Video_PPT.pptx
"""
import io
import os
import sys

from PIL import Image, ImageEnhance, ImageFilter
from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR
from pptx.util import Inches, Pt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ppt_lib import (ACCENT, ACCENT2, AMBER, BG, CROP_DEFAULT, DIM, EA, GREEN, LINE,
                     MUTED, MONO, PANEL, PANEL_2, RED, SANS, TEXT, WHITE, _footer,
                     annotate, arrow, badge, chip, header, hline, leader, load_img,
                     place, place_fill, rect, section_label, stamp, table, text, vline)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOT = os.path.join(ROOT, "docs", "prtsc")
OUT = os.path.join(ROOT, "CompilePrinciple_Video_PPT.pptx")
TOTAL = 24          # 23 页正文 + 1 页隐藏附录
HERO = (0.40, 0.46, 12.53, 5.72)     # 图区：0.46 ~ 6.18
CAP_Y = 6.30                          # 图下标注条：胶囊一律在图外，绝不压字
NOTE_Y = 6.66                         # 标注条下的一行小注

# 截图裁剪：(左, 上, 右, 下) —— 均为要去掉的百分比；目标是只留内容区，
# 去掉窗口标题栏、空白区与日志 Dock，让有效字号放大 2~3 倍。
# 底边另有 snap_bottom() 自动吸附到行间空白，杜绝“半行字”。
CROPS = {
    "nfa":       (0.00, 0.180, 0.72, 0.502),   # 状态表：0~11 行，只留用到的那几列
    "scanner":   (0.00, 0.490, 0.74, 0.240),   # ACC[] 区间表及其前几行
    "lex":       (0.00, 0.168, 0.62, 0.462),   # sample.lex 头部 + 登记表
    "ff":        (0.00, 0.158, 0.44, 0.300),   # FIRST / FOLLOW（非终结符+BNF+Follow）
    "ptable":    (0.00, 0.190, 0.320, 0.368),   # 分析表：右边界止于 KW_WHILE 列分隔线
    "lex_tab":   (0.00, 0.240, 0.66, 0.470),   # token 表
    "lex_top":   (0.900, 0.188, 0.00, 0.788),  # 只抠“✓ 真实生成扫描器产出”这一小段，放大到能读
    "parse":     (0.00, 0.190, 0.45, 0.262),   # 统计行 + 语法树 + 四元式（避开列表视口切出的半行）
    "treeir":    (0.00, 0.190, 0.50, 0.258),   # 树 + 四元式
}


def snap_bottom(path, crop, thresh=6, pad=3):
    """把裁剪底边上移到最近的行间空白，避免出现被切一半的代码行。"""
    import numpy as np
    a = np.asarray(Image.open(path).convert("L"))
    h = a.shape[0]
    ink = (a > 95).sum(axis=1)
    l, t, r, b = crop
    y1 = int((1 - b) * h)
    if y1 <= 2 or y1 >= h or ink[y1 - 2:y1].max() <= thresh:
        return crop                      # 底部本来就在空白处
    y = y1 - 1
    while y > 0 and ink[y] > thresh:     # 退到这一行的顶部
        y -= 1
    while y > 0 and ink[y] <= thresh:    # 再退过行间空白
        y -= 1
    y = min(h - 1, y + 1 + pad)
    return (l, t, r, 1 - y / h)


def shot(name):
    return os.path.join(SHOT, name)


def img(name, crop=CROP_DEFAULT, up=1):
    """up>1：裁完先 LANCZOS 上采样再交付，抵消放映时的插值发虚。"""
    p = shot(name)
    return load_img(p, snap_bottom(p, crop) if crop else crop, up=up)


def add_slide(prs):
    s = prs.slides.add_slide(prs.slide_layouts[6])
    s.background.fill.solid()
    s.background.fill.fore_color.rgb = BG
    return s


def notes(slide, dur, keyline, body, cue=None):
    tf = slide.notes_slide.notes_text_frame
    paras = [f"【本页时长】{dur}", f"【本页金句】{keyline}"]
    if cue:
        paras.append(f"【本页动作】{cue}")
    paras.append("—— 以下为演讲逐字稿，可直接照念 ——")
    paras += body if isinstance(body, list) else [body]
    tf.text = paras[0]
    for p in paras[1:]:
        para = tf.add_paragraph()
        para.text = p


def _tw(s, size):
    """估算一段文本在给定字号下的宽度（英寸）。"""
    cjk = sum(1 for c in s if ord(c) > 127)
    ascii_ = len(s) - cjk
    return cjk * size / 72.0 * 1.02 + ascii_ * size / 72.0 * 0.60


def chain(slide, x, y, w, items, h=0.40, size=9):
    """流程链：按文本长度分配宽度，避免折行。"""
    n = len(items)
    gap = 0.26
    avail = w - gap * (n - 1)
    weights = [max(_tw(it, size), 0.42) + 0.26 for it in items]
    total = sum(weights)
    widths = [avail * wt / total for wt in weights]
    cx = x
    for i, it in enumerate(items):
        pw = widths[i]
        last = i == n - 1
        chip(slide, cx, y, pw, h, it, size=size,
             color=(ACCENT2 if last else MUTED),
             fill=(RGBColor(0x1C, 0x36, 0x4E) if last else PANEL_2),
             line=(ACCENT if last else LINE), mono=True)
        if i < n - 1:
            arrow(slide, cx + pw + 0.065, y + h / 2 - 0.05, 0.11, 0.10, DIM)
        cx += pw + gap


def vlabel(slide, cx, cy, length, label, size=9, color=DIM, bold=True, w0=0.26):
    """竖排文字：以 (cx,cy) 为中心竖着写，避免被窄框压成一字一行。"""
    box = text(slide, cx - length / 2, cy - w0 / 2, length, w0, [label],
               size=size, color=color, bold=bold, align="c", anchor="m",
               wrap=False, spacing=80)
    box.rotation = 270
    return box


def stat(slide, x, y, w, num, unit, label, num_color=ACCENT, size=30):
    text(slide, x, y, w, 0.62, [[{"text": num, "size": size, "bold": True, "color": num_color, "mono": True},
                                 {"text": f" {unit}", "size": 10.5, "color": MUTED}]], anchor="b")
    text(slide, x, y + 0.62, w, 0.26, [label], size=9.5, color=MUTED)


# ---------------------------------------------------------------- 截图页骨架
def shot_head(slide, tag, page_no, side=None):
    """截图页的最小页头：左上角章节标签，右下角页码；side 放右侧说明。"""
    rect(slide, 0.42, 0.19, 0.04, 0.22, fill=ACCENT)
    text(slide, 0.56, 0.16, 8.4, 0.24, [tag], size=9, color=ACCENT, bold=True, spacing=120)
    if side:
        text(slide, 9.2, 0.16, 3.72, 0.24, [side], size=8.5, color=DIM, align="r", mono=True)
    _footer(slide, page_no, TOTAL, y=7.14)


def cap_at(slide, r, label, size=9.5):
    """图片下方的蓝色标注（图外，不压图）。"""
    cw = _tw(label, size) + 0.40
    return badge(slide, r[0] + r[2] - cw, CAP_Y, label, size=size,
                 fill=ACCENT, color=BG, w=cw, h=0.30)


def tag_at(slide, r, label, size=8.5, color=RED):
    """图片下方的红色说明（解释红框含义），也在图外。"""
    cw = _tw(label, size) + 0.34
    return badge(slide, r[0], CAP_Y, label, size=size, fill=color, color=BG, w=cw, h=0.30)


def hero(slide, src, box=HERO):
    return place(slide, src, box, border=True, border_color=RGBColor(0x2A, 0x44, 0x5C))


# ===================================================================== 封面
def p01(prs):
    s = add_slide(prs)

    im = Image.open(shot("t0_mainwindow.png")).convert("RGB")
    W, H = im.size
    cw = int(H * (5.33 / 7.5))
    im = im.crop((int(W * 0.03), 0, int(W * 0.03) + cw, H))
    im = im.resize((int(5.33 * 150), int(7.5 * 150)), Image.LANCZOS)
    im = ImageEnhance.Brightness(im).enhance(1.7)
    im = ImageEnhance.Contrast(im).enhance(1.1)
    im = im.filter(ImageFilter.GaussianBlur(4))
    base = Image.new("RGB", im.size, (0x0B, 0x16, 0x22))
    im = Image.blend(base, im, 0.24)
    buf = io.BytesIO()
    im.save(buf, "PNG")
    buf.seek(0)
    s.shapes.add_picture(buf, Inches(8.0), Inches(0), Inches(5.333), Inches(7.5))
    vline(s, 8.0, 0, 7.5, LINE, 1.0)

    rect(s, 0.62, 1.02, 0.045, 0.26, fill=ACCENT)
    text(s, 0.78, 1.00, 6.8, 0.26, ["编译原理课程设计 · 期末汇报"], size=9.5, color=ACCENT, bold=True, spacing=140)

    text(s, 0.62, 1.50, 7.2, 1.75, [[{"text": "配置驱动的编译器前端", "size": 38, "bold": True, "color": TEXT}],
                                    [{"text": "自动生成与验证系统", "size": 38, "bold": True, "color": TEXT}]],
         line_spacing=1.22)

    hline(s, 0.62, 3.40, 1.5, ACCENT, 1.5)
    text(s, 0.62, 3.58, 7.0, 0.9,
         [[{"text": "正则 → 最小 DFA → 生成扫描器", "size": 12.5, "color": MUTED, "mono": True}],
          [{"text": "BNF → LALR(1) → 四元式 IR", "size": 12.5, "color": MUTED, "mono": True}]],
         line_spacing=1.45)

    # 署名：真实信息，下划线长度按内容收紧，保持表单对齐
    rows = [("姓　　名", "曾理", 1.34, False),
            ("学　　号", "student-id", 2.42, True),
            ("指导教师", "黄煜廉", 1.34, False)]
    for i, (k, v, lw, mo) in enumerate(rows):
        yy = 4.72 + i * 0.62
        text(s, 0.62, yy + 0.06, 0.95, 0.26, [k], size=10, color=DIM, spacing=40)
        text(s, 1.60, yy, 2.9, 0.36, [v], size=18, color=TEXT, mono=mo)
        hline(s, 1.60, yy + 0.42, lw, LINE, 0.75)

    text(s, 0.62, 6.72, 7.0, 0.26, ["C++17  ·  Qt 6.11  ·  CMake  ·  13/13 CTest"], size=9, color=DIM, mono=True, spacing=40)
    text(s, 8.35, 6.72, 4.36, 0.26, ["14:35  /  23 页"], size=9, color=DIM, align="r", mono=True)

    notes(s, "0:00–0:14（14 秒）", "题目 + 姓名 + 十四分钟",
          ["各位老师好，我是曾理。我汇报的题目是《配置驱动的编译器前端自动生成与验证系统》。接下来十四分钟，我按三个任务的链路讲实现，只讲我自己跑出来的数字。"],
          cue="封面停留 14 秒，说完直接翻页，不要在这里补充背景。")
    return s


# ===================================================================== 第 2 页
def p02(prs):
    s = add_slide(prs)
    header(s, "OVERVIEW  /  总览", "三个任务，一条链路", 2, TOTAL)

    rows = [
        ("T1", "词法分析器生成",
         [".rules", "NFA", "DFA", "最小 DFA", "生成 C++ 扫描器", "sample.lex"]),
        ("T2", "语法分析器生成",
         [".bnf", "化简", "FIRST/FOLLOW", "LR(0)", "LALR(1)", "分析表", "语法树"]),
        ("T3", "端到端验收",
         ["Mini-C 真实产物", "1370 节点语法树", "139 条四元式"]),
    ]
    y = 1.50
    for tag, name, items in rows:
        rect(s, 0.62, y, 12.09, 1.16, fill=PANEL, line=LINE, radius=0.02)
        rect(s, 0.62, y, 0.05, 1.16, fill=ACCENT)
        text(s, 0.84, y + 0.13, 1.0, 0.28, [tag], size=14, color=ACCENT, bold=True, mono=True)
        text(s, 0.84, y + 0.52, 1.60, 0.42, [name], size=9.5, color=MUTED, line_spacing=1.15)
        chain(s, 2.58, y + 0.35, 9.95, items, h=0.46, size=9.5)
        y += 1.30

    # 完成度
    done = [("任务一", "规则 → 独立扫描器"), ("任务二", "文法 → 分析表 + 树"), ("任务三", "Mini-C 端到端")]
    x = 0.62
    for k, v in done:
        text(s, x, 5.56, 0.34, 0.28, ["✓"], size=13, color=GREEN, bold=True)
        text(s, x + 0.32, 5.58, 1.6, 0.26, [k], size=10, color=TEXT, bold=True)
        text(s, x + 1.72, 5.60, 2.0, 0.24, [v], size=8.5, color=DIM)
        x += 4.05

    rect(s, 0.62, 6.06, 12.09, 0.60, fill=RGBColor(0x20, 0x2C, 0x1F), line=RGBColor(0x3A, 0x4A, 0x2E), radius=0.02)
    text(s, 0.86, 6.06, 11.6, 0.60,
         [[{"text": "两项选做按必做实现　", "size": 10.5, "color": AMBER, "bold": True},
           {"text": "词法分析源程序生成  ·  中间代码生成　——　交付的不是三个片段，是一条从规则文本到四元式的完整链路。", "size": 9.5, "color": MUTED}]],
         anchor="m")

    notes(s, "0:14–0:52（38 秒）", "三个任务一条链，选做按必做",
          ["这次课程设计是三个递进的任务。任务一，从一份 `.rules` 规则文件，自动生成一个能独立编译、独立运行的扫描器；任务二，从一份 `.bnf` 文法，自动构造 LALR(1) 分析表，并且真的把语法树建出来；任务三，用 Mini-C 把前两条链缝在一起，做端到端验收。",
           "任务书里标“选做”的两项——词法分析源程序生成、中间代码生成——我全部按必做做了。所以交付的不是三个片段，是一条从规则文本一直到四元式的完整链路，装在一个 Qt 应用里，用三个 Tab 切换。"],
          cue="三条流程链从左往右念关键词，不要读每个方块的全部文字。")
    return s


# ===================================================================== 第 3 页
def p03(prs):
    s = add_slide(prs)
    header(s, "ARCHITECTURE  /  架构", "四层架构，依赖严格单向", 3, TOTAL)

    layers = [
        ("01", "呈现层", "Qt6 Widgets",
         "MainWindow  +  Task1 / 2 / 3 Page  +  3 × QThread Worker", "有 Qt"),
        ("02", "编排层", "零 Qt",
         "LexerPipeline  /  ParserPipeline  /  JointPipeline  ——  阶段枚举 + 快照", "零 Qt"),
        ("03", "算法层", "C++17 + STL",
         "regex   lexer   codegen   grammar   parser   ir", "零 Qt"),
        ("04", "数据层", "零 Qt",
         "core —— Token / Grammar / Nfa / Dfa / ParseTree / Diagnostic", "零 Qt"),
    ]
    y = 1.52
    positions = []
    for i, (no, name, tech, detail, tag) in enumerate(layers):
        h = 1.02
        rect(s, 0.62, y, 8.62, h, fill=PANEL, line=LINE, radius=0.02)
        rect(s, 0.62, y, 0.05, h, fill=(ACCENT if i < 2 else ACCENT2))
        text(s, 0.82, y + 0.10, 0.5, 0.3, [no], size=11, color=DIM, mono=True)
        text(s, 1.30, y + 0.13, 1.6, 0.32, [name], size=15, color=TEXT, bold=True)
        text(s, 1.30, y + 0.58, 5.6, 0.32, [detail], size=9, color=MUTED, mono=True)
        text(s, 7.02, y + 0.13, 2.05, 0.28, [tech], size=9, color=ACCENT, align="r", mono=True)
        text(s, 7.02, y + 0.58, 2.05, 0.28, [tag], size=8.5, color=DIM, align="r")
        positions.append((y, h))
        y += h + 0.15

    # 大括号标注零 Qt（第 2~4 层）
    top = positions[1][0]
    bottom = positions[3][0] + positions[3][1]
    brace = s.shapes.add_shape(MSO_SHAPE.LEFT_BRACE, Inches(9.28), Inches(top),
                               Inches(0.20), Inches(bottom - top))
    brace.fill.background()
    brace.line.color.rgb = GREEN
    brace.line.width = Pt(1.0)
    try:
        brace.shadow.inherit = False
    except Exception:
        pass
    vlabel(s, 9.66, (top + bottom) / 2, 2.2, "零 Qt 依赖", size=9, color=GREEN)

    # 依赖方向
    vline(s, 10.06, 1.56, 3.60, LINE, 0.75)
    arrow(s, 10.01, 5.16, 0.10, 0.14, ACCENT, "down")
    vlabel(s, 10.32, 3.36, 3.2, "依赖只往下，不回指", size=8.5, color=ACCENT, bold=False)

    # 右侧收益卡
    rect(s, 10.95, 1.52, 1.76, 4.29, fill=PANEL, line=LINE, radius=0.02)
    text(s, 11.10, 1.66, 1.5, 0.24, ["零 Qt 的收益"], size=8.5, color=ACCENT, bold=True, spacing=40)
    items = [("13", "项 CTest"), ("0", "个 QApplication"), ("4", "个命令行入口")]
    yy = 2.06
    for n, lab in items:
        text(s, 11.10, yy, 1.4, 0.42, [n], size=22, color=TEXT, bold=True, mono=True)
        text(s, 11.10, yy + 0.42, 1.4, 0.26, [lab], size=8, color=MUTED)
        yy += 1.05
        if yy < 4.4:
            hline(s, 11.10, yy - 0.14, 1.45, LINE, 0.5)

    text(s, 0.62, 6.54, 12.09, 0.3,
         [[{"text": "runThrough(stage)", "size": 10, "color": ACCENT, "mono": True},
           {"text": "  ——  界面单步按钮与命令行共享同一实现（同一份代码，只有调用者不同）", "size": 9.5, "color": MUTED}]])

    notes(s, "0:52–1:37（45 秒）", "四层架构，算法层零 Qt",
          ["系统按四层组织。最上面是呈现层，一个主窗口加三个任务页，每页配一个 QThread 工作对象，把耗时的构造过程挪出 UI 线程。第二层是编排层，三个流水线类，用阶段枚举把长流程切成可以单独触发的小步——界面上那些“只生成 NFA”“只做 LALR(1)”的按钮，和命令行走的是同一份代码。",
           "真正的重点在下面两层。算法层六个静态库，加上数据层 `core`，这两层完全不依赖 Qt。带来的直接好处是：全部 13 项单元测试都不需要创建 QApplication，在没有图形环境的命令行里就能把报告里的每个数字重跑一遍。这一点后面还会反复用到。"],
          cue="念到“下面两层”时用手势指下半屏，语速放慢半拍。")
    return s


# ===================================================================== 第 4 页
def p04(prs):
    s = add_slide(prs)
    header(s, "SCALE  /  规模与约束", "三个数字，一条硬约束", 4, TOTAL)

    nums = [("9", "个", "模块（静态库 + 可执行）"), ("1.45", "万行", "源码（C++17，不含生成物）"), ("0.54", "万行", "单元测试")]
    y = 1.50
    for n, u, lab in nums:
        rect(s, 0.62, y, 3.30, 1.02, fill=PANEL, line=LINE, radius=0.02)
        rect(s, 0.62, y, 0.04, 1.02, fill=AMBER)
        text(s, 0.85, y + 0.08, 3.0, 0.52, [[{"text": n, "size": 28, "bold": True, "color": AMBER, "mono": True},
                                             {"text": f"  {u}", "size": 10.5, "color": MUTED}]], anchor="b")
        text(s, 0.85, y + 0.62, 2.95, 0.28, [lab], size=8.2, color=MUTED)
        y += 1.14

    text(s, 0.62, 4.94, 3.30, 0.72,
         [[{"text": "cmake -B build && cmake --build build", "size": 7.4, "color": ACCENT2, "mono": True}],
          [{"text": "一条命令完成整套构建，没有额外脚本。", "size": 8, "color": DIM}]], line_spacing=1.45)

    # 技术栈
    section_label(s, 4.22, 1.44, 3.0, "技术栈  /  STACK")
    tags = [("C++17", 0.95), ("Qt 6.11 Widgets", 1.62), ("CMake ≥ 3.16", 1.42), ("g++ 13.1", 1.10), ("MinGW 64-bit", 1.42)]
    xx = 4.22
    for t, w in tags:
        chip(s, xx, 1.72, w, 0.34, t, size=9, color=ACCENT2, mono=True)
        xx += w + 0.12

    # 证据链
    section_label(s, 4.22, 2.24, 5.0, "零 Qt 依赖  /  一条可验证的证据链")
    steps = [("1", "core / lexer", "不 include 任何 Qt 头文件"),
             ("2", "生成源码", "无第三方依赖，g++ -std=c++17 独立编译通过"),
             ("3", "输出比对", "与进程内扫描逐 token 一致")]
    x = 4.22
    for i, (no, head, body) in enumerate(steps):
        rect(s, x, 2.56, 2.58, 1.42, fill=PANEL, line=LINE, radius=0.02)
        text(s, x + 0.14, 2.66, 0.3, 0.24, [no], size=9, color=ACCENT, mono=True, bold=True)
        text(s, x + 0.14, 2.92, 2.3, 0.26, [head], size=10.5, color=TEXT, bold=True, mono=True)
        text(s, x + 0.14, 3.22, 2.26, 0.70, [body], size=8.3, color=MUTED, line_spacing=1.35)
        if i < 2:
            arrow(s, x + 2.62, 3.16, 0.14, 0.12, ACCENT)
        x += 2.78

    # ctest 截图：只截尾部三行（100% passed / total time / 提示符），放大到能读
    cc = (0.0, 0.872, 0.56, 0.004)
    rt = place(s, load_img(shot("t1_unit_test.png"), snap_bottom(shot("t1_unit_test.png"), cc), up=2),
               (4.22, 4.26, 6.30, 0.94))
    stamp(s, 11.62, 4.74, 0.46, "13/13", "TESTS PASSED")
    text(s, 4.22, 5.34, 7.30, 0.52,
         [[{"text": "ctest --test-dir build", "size": 8.5, "color": ACCENT, "mono": True},
           {"text": "　13 项全过，0 失败", "size": 8.5, "color": DIM}],
          [{"text": "不需要 QApplication，也不需要显示环境 —— 命令行里能完整复现。", "size": 8.5, "color": MUTED}]],
         line_spacing=1.5)

    # 金句
    rect(s, 0.62, 6.34, 12.09, 0.44, fill=RGBColor(0x1B, 0x2A, 0x1C), line=None)
    rect(s, 0.62, 6.34, 0.05, 0.44, fill=AMBER)
    text(s, 0.82, 6.34, 11.8, 0.44,
         [[{"text": "全工程没有一处 ", "size": 10.5, "color": MUTED},
           {"text": "KW_IF", "size": 10.5, "color": AMBER, "mono": True},
           {"text": " 这样的硬编码 —— 换语言只换一个目录。", "size": 10.5, "color": TEXT, "bold": True}]], anchor="m")

    notes(s, "1:37–2:22（45 秒）", "9 模块 / 1.45 万行 / 13 全绿",
          ["先用三个数字交代规模：9 个模块、源码约 1.45 万行、单元测试约 5400 行。技术栈是 C++17 加 Qt6 Widgets，构建用 CMake，一条 cmake 命令，没有额外脚本。",
           "这里我想强调“零 Qt 依赖”不是一句口号，它是一条可以验证的约束。`core` 和 `lexer` 两层不 include 任何 Qt 头文件，所以生成出来的扫描器可以拿到工程外面，用一条 `g++ -std=c++17` 单独编译；跑出来的 `sample.lex` 和工程内进程内扫描的结果逐 token 一致。这件事看着只是“解耦做得好”，其实是任务三能成立的前提——任务三要验的是真实进程的产物，不是内存里的对象。这张是 ctest 的结果，13 项全部通过，0 失败。"],
          cue="c 讲“零 Qt 依赖”时手指证据链三个方块，不要念方框里的小字。")
    return s


# ===================================================================== 第 5 页（文字）
def p05(prs):
    s = add_slide(prs)
    header(s, "TASK 01  /  词法分析 · 规则 → NFA", "任务一 · 把规则文本变成自动机", 5, TOTAL,
           sub="输入一份 .rules：四类指令决定语言级行为，规则行决定单词集合。")

    chips = [("@name", 0.98), ("@case-sensitive", 1.62), ("@ignore", 0.92), ("@comment", 1.12)]
    xx = 0.62
    for t, w in chips:
        chip(s, xx, 1.72, w, 0.34, t, size=9, color=ACCENT2, mono=True)
        xx += w + 0.12
    text(s, 5.86, 1.74, 6.85, 0.26,
         [[{"text": "登记名 = 模式", "size": 9.5, "color": TEXT, "mono": True},
           {"text": "　每条规则独立建片段，最后拼成一张图", "size": 8.5, "color": DIM}]])

    cards = [
        ("01", "关键字不进自动机",
         ["8 个关键字与标识符的正则，在 DFA 里会大量重合，建进去只白涨状态数。",
          "自动机只识别“看起来像标识符”的词，命中之后再用关键字表，把小写折叠后的拼写重分类。"],
         "READ / read / Read  同落 108 号 KW_READ"),
        ("02", "编码约定",
         ["0 固定为 EOF，255 固定为 ERROR，语言包只分配 1..N。",
          "任何语言包生成的 sample.lex，都能被同一个读取器消费。"],
         "读取器不认语言，只认编码约定"),
    ]
    x = 0.62
    for no, title, pars, tail in cards:
        rect(s, x, 2.32, 5.92, 1.92, fill=PANEL, line=LINE, radius=0.02)
        rect(s, x, 2.32, 0.04, 1.92, fill=ACCENT)
        text(s, x + 0.20, 2.44, 0.36, 0.24, [no], size=9, color=ACCENT, mono=True, bold=True)
        text(s, x + 0.56, 2.42, 5.0, 0.30, [title], size=12.5, color=TEXT, bold=True)
        text(s, x + 0.20, 2.82, 5.5, 0.98,
             [[{"text": pars[0], "size": 9.3, "color": MUTED}],
              [{"text": pars[1], "size": 9.3, "color": MUTED}]], line_spacing=1.45)
        text(s, x + 0.20, 3.86, 5.5, 0.26, [tail], size=8.8,
             color=ACCENT2 if no == "01" else MUTED, mono=(no == "01"))
        x += 6.17

    # Thompson 构造
    section_label(s, 0.62, 4.44, 6.0, "THOMPSON 构造  /  五步拼成一张图")
    chain(s, 0.62, 4.70, 12.09, ["正则文本", "递归下降 AST", "Thompson 片段", "偏移平移合并", "空串校验"], h=0.46, size=10)

    rect(s, 0.62, 5.38, 7.90, 1.30, fill=PANEL, line=LINE, radius=0.02)
    text(s, 0.80, 5.48, 7.55, 1.12,
         [[{"text": "① 每条规则独立建一个小 NFA　② 按当前状态数整体平移，拼成总图　③ 新建总起点，用 ε 连各条入口", "size": 9, "color": MUTED}],
          [{"text": "④ 从总起点求 ε 闭包：若闭包含接受态，说明某条规则能匹配空串 → 直接报错", "size": 9, "color": TEXT}],
          [{"text": "不报的话，扫描时指针不前进，会死循环。", "size": 9, "color": DIM}]], line_spacing=1.45)

    rect(s, 8.70, 5.38, 4.01, 1.30, fill=RGBColor(0x14, 0x2C, 0x3E), line=None, radius=0.02)
    rect(s, 8.70, 5.38, 0.04, 1.30, fill=ACCENT)
    text(s, 8.92, 5.48, 3.65, 1.10,
         [[{"text": "TINY 实测", "size": 8.5, "color": ACCENT, "bold": True, "spacing": 80}],
          [{"text": "26 条规则", "size": 11, "color": TEXT, "bold": True, "mono": True},
           {"text": " ｜ 8 条关键字不进自动机", "size": 8.5, "color": MUTED}],
          [{"text": "18 条参与构造 → ", "size": 8.5, "color": MUTED},
           {"text": "121", "size": 13, "color": AMBER, "bold": True, "mono": True},
           {"text": " 个状态", "size": 8.5, "color": MUTED}]], line_spacing=1.4)

    notes(s, "2:22–3:16（54 秒）", "关键字不进自动机，编码 0 与 255 保留",
          ["任务一的核心是两段自动转换。一份 `.rules`，四类指令决定语言级行为，规则行决定单词集合。有两个设计决定值得说。",
           "第一，关键字只登记编码、不进自动机。八个关键字和标识符的正则在 DFA 里大量重合，建进去只白涨状态数。做法是：自动机只识别“看起来像标识符”的词，命中后再用关键字表按小写折叠重分类。所以 TINY 不区分大小写时，READ、read、Read 都落到 108 号 KW_READ。",
           "第二，编码 0 固定为 EOF、255 固定为 ERROR，语言包只分配 1 到 N。这条约定让任何语言的 `sample.lex` 都能被同一个读取器消费。",
           "构造走 Thompson：每条规则独立建小 NFA，按当前状态数平移拼成总图，新建总起点用 ε 连各条入口。拼完还有一道校验——从总起点求 ε 闭包，闭包含接受态就说明有规则能匹配空串，直接报错。下面看真实的构造结果，请看屏幕。"],
          cue="最后一句“请看屏幕”说完再点下一页，中间停半秒。")
    return s


# ===================================================================== 第 6 页（截图）
def p06(prs):
    s = add_slide(prs)
    shot_head(s, "TASK 01  /  截图 · TINY NFA 状态表", 6, "1919 × 1033  ·  本页只看这一张")

    r = hero(s, img("t1_tiny_nfa.png", CROPS["nfa"], up=3), HERO)
    annotate(s, r, CROPS["nfa"], 0.004, 0.186, 0.135, 0.019, lw=1.6)

    badge(s, r[0], CAP_Y, "红框 = 0 号总起点：ε 连各条规则入口", size=9, fill=RED, color=BG,
          w=_tw("红框 = 0 号总起点：ε 连各条规则入口", 9) + 0.40, h=0.30)
    lbl = "TINY NFA 实测：18 条规则 → 121 个状态"
    cw = _tw(lbl, 9) + 0.44
    badge(s, r[0] + r[2] - cw, CAP_Y, lbl, size=9, fill=ACCENT, color=BG, w=cw, h=0.30)
    text(s, r[0], NOTE_Y, r[2], 0.30,
         [[{"text": "表里只截到 10 号状态，完整 121 个状态见图 2-3。", "size": 9, "color": DIM},
           {"text": "　左边是状态号，中间是字符与目标状态，最后一列是这一行的出边条数。", "size": 9, "color": MUTED}]])

    notes(s, "3:16–3:34（18 秒）", "121 个状态，一行一条边",
          ["这就是构造出来的状态表。0 号是总起点，用 ε 把各条规则的入口连起来；后面每一行是“状态、符号、目标”，最后是这一行出边的条数。TINY 十八条参与构造的规则，合并之后得到 121 个状态。"],
          cue="此页只放图，停留 18 秒；手指红框处，看完直接翻页，不补充细节。")
    return s


# ===================================================================== 第 7 页（文字）
def p07(prs):
    s = add_slide(prs)
    header(s, "TASK 01  /  词法分析 · 生成源码", "任务一 · 把自动机压成 700 行 C++", 7, TOTAL,
           sub="不输出 256 列矩阵：按字节排序合并区间，再生成一个只依赖标准库的扫描器。")

    # 左：收敛 + 区间压缩
    section_label(s, 0.62, 1.72, 4.4, "自动机收敛")
    rows = [("NFA", "121", "Thompson 构造", MUTED), ("DFA", "52", "子集构造", MUTED), ("最小 DFA", "50", "Hopcroft 最小化", ACCENT)]
    y = 1.98
    for name, num, how, col in rows:
        rect(s, 0.62, y, 4.20, 0.58, fill=PANEL, line=(ACCENT if col == ACCENT else LINE), radius=0.02)
        text(s, 0.78, y + 0.15, 1.6, 0.28, [name], size=10, color=TEXT, bold=True)
        text(s, 2.50, y + 0.10, 0.76, 0.36, [num], size=16, color=col, bold=True, mono=True, align="r")
        text(s, 3.34, y + 0.17, 1.40, 0.26, [how], size=7.8, color=DIM, align="r")
        y += 0.58
        if name != "最小 DFA":
            arrow(s, 2.60, y + 0.02, 0.12, 0.14, DIM, "down")
            y += 0.18

    section_label(s, 0.62, 4.20, 4.4, "区间压缩  /  10 个转移 → 1 个判断")
    rect(s, 0.62, 4.46, 4.20, 1.52, fill=PANEL, line=LINE, radius=0.02)
    text(s, 0.78, 4.54, 3.9, 0.24, ["50 态 × 256 字节 ≈ 1.28 万个整数"], size=8, color=DIM)
    for i in range(10):
        rect(s, 0.78 + i * 0.25, 4.80, 0.22, 0.26, fill=PANEL_2, line=LINE, radius=0.02)
        text(s, 0.78 + i * 0.25, 4.80, 0.22, 0.26, [str(48 + i)], size=6.5, color=ACCENT2, mono=True, align="c", anchor="m")
    text(s, 3.32, 4.84, 0.3, 0.24, ["…"], size=10, color=DIM)
    arrow(s, 1.60, 5.10, 0.10, 0.11, ACCENT, "down")
    text(s, 0.78, 5.26, 3.9, 0.30, ["c >= 48 && c <= 57"], size=10.5, color=ACCENT, mono=True)
    text(s, 0.78, 5.58, 3.9, 0.24, ["长度 1：c == k　长度 2：两个或　≥3：范围判断"], size=7.5, color=DIM)

    text(s, 0.62, 6.14, 4.30, 0.60,
         [[{"text": "52 → 50", "size": 8.5, "color": ACCENT2, "mono": True},
           {"text": " 只合并了 2 个等价态，但生成源码从矩阵的 1.28 万个数降到 700 行。", "size": 8, "color": DIM}]], line_spacing=1.4)

    # 右：三张说明卡
    cards = [
        ("01", "生成源码：700 行，逐行可核对",
         ["按状态展开成 switch，每个 case 里是按字节排序的区间判断；",
          "只 include 标准库，g++ -std=c++17 一条命令能编过。"],
         "generated_lexer.cpp"),
        ("02", "sample.lex：自描述，任务二不必再读 .rules",
         ["头部是“编码、名称、显示名”的登记表，数据区是“编码、词素”。",
          "任务二拿到它能还原全部编码语义——两个任务唯一的耦合点。"],
         "#file-format: sample.lex"),
        ("03", "最长匹配：两个变量回退",
         ["每进入一个接受态记一次位置，走不动就退回最近记录点。",
          "少记一次，<= 就会被切成 < 和 =，而且在单字符用例上看不出来。"],
         "lastAccept / lastAcceptPos"),
    ]
    y = 1.72
    for no, head, body, mono in cards:
        rect(s, 5.14, y, 7.57, 1.44, fill=PANEL, line=LINE, radius=0.02)
        rect(s, 5.14, y, 0.04, 1.44, fill=(ACCENT if no != "03" else RED))
        text(s, 5.34, y + 0.12, 0.4, 0.22, [no], size=8.5, color=ACCENT, bold=True, mono=True)
        text(s, 5.68, y + 0.10, 6.8, 0.28, [head], size=11.5, color=TEXT, bold=True)
        text(s, 5.34, y + 0.46, 7.18, 0.64,
             [[{"text": body[0], "size": 8.8, "color": MUTED}],
              [{"text": body[1], "size": 8.8, "color": MUTED}]], line_spacing=1.4)
        text(s, 5.34, y + 1.12, 7.18, 0.24, [mono], size=8.2,
             color=ACCENT2 if no != "03" else AMBER, mono=True)
        y += 1.56

    notes(s, "3:34–4:30（56 秒）", "区间压缩 + 自描述 lex + 最长匹配回退",
          ["子集构造得到 52 个 DFA 状态，Hopcroft 最小化后剩 50 个。接下来是整章我最喜欢的一段。",
           "系统不输出 256 列的转移矩阵——既占篇幅又没法读。做法是把每个状态的转移按字节排序，合并成“连续、同目标”的区间：长度 1 写成 `c == k`，长度 2 写成两个或，三个以上写成范围判断。压缩完，生成源码不到 700 行，可以逐行核对。",
           "然后调 g++ 编译成 `tiny_scanner`，运行产出 `sample.lex`。这份文件是自描述的：头部是“编码、名称、显示名”，数据区是“编码、词素”。任务二拿到它就能还原全部编码语义，不必再读一次 `.rules`——这是两个任务之间唯一的耦合点。",
           "最长匹配靠两个变量回退：每进入一个接受态记一次位置，走不动就退回最近记录点。少记一次，`<=` 就被切成 `<` 和 `=` 两个 token，而这种错误在单字符运算符的用例上看不出来。下面看生成出来的代码，请看屏幕。"],
          cue="讲到“下面看代码”时先停半秒，再翻页。")
    return s


# ===================================================================== 第 8 页（截图）
def p08(prs):
    s = add_slide(prs)
    shot_head(s, "TASK 01  /  截图 · 生成的扫描器源码", 8, "红框 = ACC[] 区间表")

    r = hero(s, img("t1_tiny_scanner.png", CROPS["scanner"], up=3))
    annotate(s, r, CROPS["scanner"], 0.0225, 0.6075, 0.152, 0.0945, lw=1.3)
    tag_at(s, r, "红框 = ACC[][3] 区间表")
    cap_at(s, r, "generated_lexer.cpp · 700 行 · 只依赖标准库")

    notes(s, "4:30–4:45（15 秒）", "ACC[] 就是那张区间表",
          ["这是生成出来的 `generated_lexer.cpp`。红框这里是 `ACC` 数组——就是刚才说的区间表，十个连续字节压成一个区间判断。整份文件不到七百行，没有一个第三方依赖。"],
          cue="此页只放图，停留 15 秒；手指红框，不要逐行读代码。")
    return s


# ===================================================================== 第 9 页（截图）
def p09(prs):
    s = add_slide(prs)
    shot_head(s, "TASK 01  /  截图 · sample.lex 头部", 9, "红框 = 自描述头部")

    r = hero(s, img("t1_tiny_lex.png", CROPS["lex"], up=2))
    annotate(s, r, CROPS["lex"], 0.004, 0.1725, 0.336, 0.0675, lw=1.6)
    cap_at(s, r, "sample.lex 头部：24 个登记项，自描述")

    notes(s, "4:45–5:00（15 秒）", "lex 自描述，任务二不用再读 .rules",
          ["这是运行之后产出的 `sample.lex`。上面是头部：编码、名称、显示名，二十八个登记项；下面是数据区，编码加词素。任务二只读这个头部，就能对齐全部终结符，不用再读一次规则文件。"],
          cue="此页只放图，停留 15 秒；手指头部那几行。")
    return s


# ===================================================================== 第 10 页（文字）
def p10(prs):
    s = add_slide(prs)
    header(s, "TASK 01  /  实测", "任务一 · 两个语言包的实测", 10, TOTAL,
           sub="同一份代码，换一个语言包目录；两列的差别只在规模与字母表，不在流程。")

    rows = [
        ["指标", "TINY", "Mini-C"],
        ["规则数 / 登记项", "26 / 28", "35 / 37"],
        ["NFA → DFA → 最小 DFA", "121 → 52 → 50", "165 → 70 → 66"],
        ["扫描 token 数", "104（+EOF）", "416（+EOF）"],
        ["ERROR 数", ("0", {"color": GREEN, "bold": True}), ("0", {"color": GREEN, "bold": True})],
    ]
    table(s, 0.62, 1.70, 12.09, [3.40, 4.30, 4.39], rows, row_h=0.71, head_h=0.50,
          size=15, head_size=10, mono_cols=(1, 2), align=["l", "c", "c"])

    rect(s, 0.62, 5.18, 12.09, 0.62, fill=PANEL, line=None)
    rect(s, 0.62, 5.18, 0.05, 0.62, fill=AMBER)
    text(s, 0.86, 5.18, 11.5, 0.62,
         [[{"text": "每个数字都有单测锚点", "size": 11.5, "color": TEXT, "bold": True},
           {"text": "，不是界面跑一次好看就写进报告。", "size": 10, "color": MUTED}]], anchor="m")

    text(s, 0.62, 5.96, 12.09, 0.85,
         [[{"text": "lexer_pipeline_test", "size": 9.5, "color": ACCENT2, "mono": True},
           {"text": " 里逐条 CHECK 钉住：规则数、NFA 状态数、DFA 状态数、最小化结果、token 数、ERROR 数 —— 改任何一处实现，对应断言立刻红。", "size": 9.5, "color": MUTED}],
          [{"text": "Mini-C 比 TINY 多 16 个最小 DFA 状态、多 33 字节字母表，但流水线一行都没有分叉。", "size": 9.5, "color": DIM}]],
         line_spacing=1.5, space_after=4)

    notes(s, "5:00–5:40（40 秒）", "两列实测，零错误",
          ["实测两列。TINY 是 26 条规则、28 个登记项，121 个 NFA 状态、52 个 DFA、最小化后 50 个，扫 104 个 token，零错误。Mini-C 是 35 条规则、37 个登记项，165 到 70 再到 66，扫 416 个 token，同样零错误。两列的差别只在规模和字母表，不在流程——同一份代码。",
           "下面这行是我做这一章时给自己定的规矩：每个数字都要有单元测试断言钉住，而不是跑一次界面好看就写进报告。所以词法这一章的每个状态数，都能在 `lexer_pipeline_test` 里找到对应的 CHECK。"],
          cue="念数字时手指表格对应行，语速放慢，让观众有时间对表。")
    return s


# ===================================================================== 第 11 页（文字）
def p11(prs):
    s = add_slide(prs)
    header(s, "TASK 02  /  语法分析 · 输入与化简", "任务二 · 输入只有两份文本", 11, TOTAL,
           sub=".bnf 和任务一产出的 sample.lex，中间没有任何语言常量。")

    stages = ["LexRead", "BnfParse", "Simplify", "FirstFollow", "Lr0", "Lalr1", "ParseTable", "Drive", "Ir"]
    x = 0.62
    bw, gap = 1.20, 0.16
    for i, st in enumerate(stages):
        active = i < 2
        chip(s, x, 1.78, bw, 0.36, st, size=8.5, mono=True,
             color=(ACCENT2 if active else DIM), fill=(PANEL_2 if active else PANEL),
             line=(ACCENT if active else LINE))
        if i < len(stages) - 1:
            arrow(s, x + bw + 0.03, 1.93, 0.09, 0.09, DIM)
        x += bw + gap

    arrow(s, 1.22, 2.21, 0.10, 0.12, AMBER, "up")
    text(s, 1.40, 2.24, 6.4, 0.24,
         [[{"text": "顺序前移：", "size": 8.8, "color": AMBER, "bold": True},
           {"text": "先读 lex 登记表，再解析 BNF —— 终结符编码只存在于 lex 头部，拿不到就无法对齐 KW_IF", "size": 8.8, "color": MUTED}]])
    text(s, 9.6, 2.24, 3.11, 0.24, ["九个阶段，每阶段可单独触发"], size=8, color=DIM, align="r")

    # 左：化简
    section_label(s, 0.62, 2.60, 4.6, "化简  /  两阶段，外层迭代到稳定")
    rect(s, 0.62, 2.88, 5.62, 1.86, fill=PANEL, line=LINE, radius=0.02)
    steps = [("①", "可终结不动点", "while(changed) 连锁删除，避免悬垂引用"),
             ("②", "从开始符号可达性", "删掉永远走不到的产生式")]
    yy = 3.00
    for no, head, body in steps:
        text(s, 0.80, yy + 0.02, 0.28, 0.24, [no], size=10, color=ACCENT, bold=True)
        text(s, 1.12, yy, 2.1, 0.24, [head], size=10, color=TEXT, bold=True)
        text(s, 3.30, yy + 0.02, 2.80, 0.24, [body], size=8.3, color=DIM)
        yy += 0.62
    hline(s, 0.80, 4.14, 5.26, LINE, 0.5)
    text(s, 0.80, 4.24, 5.24, 0.42,
         [[{"text": "删 1 条可能让某非终结符失去全部产生式 → 必须连锁删引用者", "size": 8.3, "color": DIM}]])

    text(s, 0.62, 4.86, 5.62, 0.26,
         [[{"text": "两个真实语言包都是零删除", "size": 9, "color": GREEN, "bold": True},
           {"text": "；删除路径由人造用例 ", "size": 9, "color": MUTED},
           {"text": "D-3", "size": 9, "color": ACCENT2, "mono": True},
           {"text": " 覆盖。", "size": 9, "color": MUTED}]])

    cards = [("TINY", "37", "45", ""), ("Mini-C", "78", "70", "含 5 条 ε")]
    x = 0.62
    for name, prod, sym, extra in cards:
        rect(s, x, 5.20, 2.72, 1.14, fill=PANEL, line=LINE, radius=0.02)
        text(s, x + 0.14, 5.28, 2.2, 0.24, [name], size=8.5, color=ACCENT, bold=True, mono=True)
        text(s, x + 0.14, 5.54, 1.2, 0.34, [prod], size=19, color=TEXT, bold=True, mono=True)
        text(s, x + 0.72, 5.62, 1.2, 0.24, ["条产生式"], size=7.5, color=MUTED)
        text(s, x + 0.14, 5.94, 2.5, 0.26,
             [[{"text": sym, "size": 11, "color": ACCENT2, "mono": True},
               {"text": " 个符号  ", "size": 7.5, "color": MUTED},
               {"text": extra, "size": 7.5, "color": AMBER}]])
        x += 2.90

    # 右：β 可空
    section_label(s, 6.70, 2.60, 6.0, "FIRST / FOLLOW  /  β 可空判定")
    rect(s, 6.70, 2.88, 6.01, 1.86, fill=PANEL, line=LINE, radius=0.02)
    text(s, 6.88, 2.98, 5.6, 0.26, ["给新引入的项算展望符，取 FIRST(βa)"], size=9.5, color=TEXT, bold=True)
    text(s, 6.88, 3.30, 5.6, 0.30, ["a ∈ FIRST(βa)  if  β ⇒* ε"], size=11, color=ACCENT2, mono=True)
    text(s, 6.88, 3.68, 5.62, 0.94,
         [[{"text": "β 可空时，展望符要沿到 a 之后的符号；不可空时只取 FIRST(β)。", "size": 8.8, "color": MUTED}],
          [{"text": "实现上把 a 拼在 β 后面交给同一个 firstOfString，不另写传播逻辑。", "size": 8.8, "color": DIM}]],
         line_spacing=1.45)

    rect(s, 6.70, 4.86, 6.01, 1.48, fill=RGBColor(0x14, 0x2C, 0x3E), line=None, radius=0.02)
    rect(s, 6.70, 4.86, 0.04, 1.48, fill=GREEN)
    text(s, 6.92, 4.96, 5.6, 1.28,
         [[{"text": "验证方式", "size": 8.5, "color": GREEN, "bold": True, "spacing": 80}],
          [{"text": "和手算逐格对照：FOLLOW(statement) 里必须出现 else 与 until，", "size": 8.8, "color": MUTED}],
          [{"text": "出现就说明 β 后面的符号被正确带进了展望集。", "size": 8.8, "color": MUTED}]], line_spacing=1.45)

    notes(s, "5:40–6:28（48 秒）", "先读 lex 再解析 BNF，化简迭代到稳定",
          ["任务二的输入只有两份文本：`.bnf` 和任务一产出的 `sample.lex`，中间没有任何语言常量。",
           "这里有一处顺序和任务书不同：先读 lex、再解析 BNF。因为终结符编码只存在于 lex 头部，拿不到它，`KW_IF` 就没法对齐词法编码。",
           "化简分两阶段，外层迭代到稳定。先求“可终结”，删掉左部不可终结、或右部含不可终结符号的产生式；放在 `while` 里反复做，是因为删一条可能让某个非终结符失去全部产生式，必须连锁删掉引用者，否则会留下悬垂引用。再从开始符号做可达性。两个真实语言包都是零删除，删除路径由人造用例 D-3 覆盖。FIRST/FOLLOW 的实算结果，请看屏幕。"],
          cue="“请看屏幕”说完停半秒再翻页。")
    return s


# ===================================================================== 第 12 页（截图）
def p12(prs):
    s = add_slide(prs)
    shot_head(s, "TASK 02  /  截图 · FIRST / FOLLOW", 12, "红框 = statement 行")

    r = hero(s, img("t2_tiny_firstfollow.png", CROPS["ff"], up=2), (0.40, 0.46, 12.53, 5.76))
    annotate(s, r, CROPS["ff"], 0.316, 0.275, 0.220, 0.017, lw=2.0)
    y = r[1] + r[3] + 0.18
    rect(s, 0.40, y, 12.53, 0.62, fill=RGBColor(0x14, 0x2C, 0x3E), line=None, radius=0.02)
    rect(s, 0.40, y, 0.05, 0.62, fill=GREEN)
    text(s, 0.64, y, 12.1, 0.62,
         [[{"text": "FOLLOW(statement)", "size": 13, "color": ACCENT2, "bold": True, "mono": True},
           {"text": "  ∋  ", "size": 13, "color": DIM},
           {"text": "else", "size": 13, "color": AMBER, "bold": True, "mono": True},
           {"text": " 、 ", "size": 13, "color": DIM},
           {"text": "until", "size": 13, "color": AMBER, "bold": True, "mono": True},
           {"text": "　—— β 可空判定生效的证据", "size": 10, "color": MUTED}]], anchor="m")

    notes(s, "6:28–6:46（18 秒）", "FOLLOW(statement) 里有 else 和 until",
          ["这是 TINY 的 FIRST 和 FOLLOW 表。红框这一行是 statement：看 FOLLOW 这一列，里面有 else、有 until——这正是 β 可空判定生效的证据，和手算逐格一致。"],
          cue="此页只放图，停留 18 秒；手指红框那一行，再指下面那条结论。")
    return s


# ===================================================================== 第 13 页（文字）
def p13(prs):
    s = add_slide(prs)
    header(s, "TASK 02  /  语法分析 · LALR(1)", "LALR(1) = 规范 LR(1) + 同心合并", 13, TOTAL,
           sub="路线 B：先把 LR(1) 全建出来，再按核心合并——中间产物可以单独断言。")

    steps = [
        ("01", "建规范 LR(1)", "闭包规则与 LR(0) 相同，只多算展望符", "FIRST(βa)"),
        ("02", "投影核心", "去掉展望符，拿核心去 LR(0) 索引查状态号", "查不到就报错"),
        ("03", "同核心合并", "展望符取并集，按 LR(0) id 重建状态", "状态数 = LR(0)"),
    ]
    x = 0.62
    for no, head, body, tag in steps:
        rect(s, x, 1.78, 3.88, 1.80, fill=PANEL, line=LINE, radius=0.02)
        text(s, x + 0.20, 1.88, 0.4, 0.24, [no], size=9.5, color=ACCENT, bold=True, mono=True)
        text(s, x + 0.20, 2.16, 3.4, 0.30, [head], size=13.5, color=TEXT, bold=True)
        text(s, x + 0.20, 2.54, 3.5, 0.66, [body], size=9, color=MUTED, line_spacing=1.4)
        text(s, x + 0.20, 3.24, 3.5, 0.24, [tag], size=8.5, color=ACCENT2, mono=True)
        if no != "03":
            arrow(s, x + 3.94, 2.56, 0.16, 0.12, ACCENT)
        x += 4.11

    rect(s, 0.62, 3.80, 12.09, 0.72, fill=RGBColor(0x14, 0x2C, 0x3E), line=None)
    rect(s, 0.62, 3.80, 0.05, 0.72, fill=GREEN)
    text(s, 0.84, 3.80, 11.7, 0.72,
         [[{"text": "移进集合由核心决定 → 合并不改它；归约触发面由展望符决定 → 合并只会变大。", "size": 10.5, "color": TEXT},
           {"text": "　故：只可能新增 R/R，绝不新增 S/R。", "size": 10.5, "color": ACCENT2, "bold": True}]], anchor="m")

    stats = [("59", "309", "TINY：规范 LR(1) → LALR(1)", 1.02), ("128", "408", "Mini-C：规范 LR(1) → LALR(1)", 1.34)]
    x = 0.62
    for big, small, lab, dx in stats:
        text(s, x, 4.70, 1.6, 0.66, [big], size=36, color=AMBER, bold=True, mono=True, anchor="b")
        text(s, x + dx, 5.02, 2.4, 0.26,
             [[{"text": "← " + small, "size": 11, "color": DIM, "mono": True}]])
        text(s, x, 5.42, 4.0, 0.26, [lab], size=9, color=MUTED)
        x += 5.30

    text(s, 0.62, 5.80, 12.09, 0.28,
         [[{"text": "合并后状态数 = LR(0) 状态数", "size": 9.5, "color": ACCENT2, "mono": True},
           {"text": "，转移数逐项相等 —— 这不是巧合，是合并算法的定义决定的。", "size": 9.5, "color": MUTED}]])

    rect(s, 0.62, 6.22, 12.09, 0.50, fill=PANEL, line=None)
    rect(s, 0.62, 6.22, 0.05, 0.50, fill=AMBER)
    text(s, 0.82, 6.22, 11.7, 0.50,
         [[{"text": "代价", "size": 9, "color": AMBER, "bold": True},
           {"text": "：中间态内存约 5 倍 —— 百级文法可接受，因为中间产物能单独断言；再大一个量级就该换成直接构造。", "size": 9, "color": DIM}]], anchor="m")

    notes(s, "6:46–7:40（54 秒）", "LR(1) 再合并，只增 R/R 不增 S/R",
          ["LALR(1) 我走“路线 B”：先建规范 LR(1)，再按核心把同心状态并起来。",
           "第一步，闭包规则和 LR(0) 一样，只多一件事：给新引入的项算展望符，取 `FIRST(βa)`。实现上把 `a` 拼在 β 后面交给同一个 `firstOfString`。",
           "第二步，把每个 LR(1) 状态投影成核心，就是去掉展望符；拿核心去 LR(0) 索引查状态号，查不到就报内部不一致，绝不静默新建。第三步，同核心的项，展望符取并集。",
           "合并的性质很干净：移进集合由核心决定，合并不改它；归约的触发面由展望符决定，合并只会变大。所以合并只可能新增归约-归约冲突，绝不新增移进-归约冲突。实测 TINY 把 309 个状态并成 59 个，Mini-C 把 408 并成 128 个，合并后的状态数和转移数与 LR(0) 逐项相等。",
           "代价在中间态：内存大约 5 倍。百级文法划算，因为中间产物能单独断言；再大一个量级就该换成直接构造。"],
          cue="讲“性质很干净”时指绿色高亮条，这是本页的金句。")
    return s


# ===================================================================== 第 14 页（文字）
def p14(prs):
    s = add_slide(prs)
    header(s, "TASK 02  /  分析表", "一张表，一处冲突，一个语义选择", 14, TOTAL,
           sub="填表是单趟扫描；所有归约走同一个出口，冲突才可能一个不漏。")

    section_label(s, 0.62, 1.74, 4.6, "填表  /  单趟扫描")
    steps = [("1", "遍历转移", "非终结符 → GOTO，终结符 → Shift"),
             ("2", "遍历完成项", "按展望符逐格写 Reduce"),
             ("3", "统一判定", "所有 Reduce 走同一个 writeReduce")]
    y = 2.00
    for no, head, body in steps:
        rect(s, 0.62, y, 5.10, 0.60, fill=PANEL, line=LINE, radius=0.02)
        text(s, 0.76, y + 0.17, 0.24, 0.24, [no], size=9.5, color=ACCENT, mono=True, bold=True)
        text(s, 1.04, y + 0.17, 1.5, 0.24, [head], size=9.5, color=TEXT, bold=True)
        text(s, 2.62, y + 0.18, 3.0, 0.24, [body], size=8.3, color=DIM)
        y += 0.64

    section_label(s, 0.62, 4.08, 4.6, "冲突  /  报告而非失败")
    rect(s, 0.62, 4.34, 5.10, 1.00, fill=PANEL, line=LINE, radius=0.02)
    text(s, 0.78, 4.44, 4.8, 0.26,
         [[{"text": "S / R", "size": 9, "color": AMBER, "mono": True},
           {"text": "  保留 Shift　　", "size": 9, "color": MUTED},
           {"text": "R / R", "size": 9, "color": AMBER, "mono": True},
           {"text": "  保留低产生式 id", "size": 9, "color": MUTED}]])
    text(s, 0.78, 4.74, 4.8, 0.54,
         [[{"text": "冲突进清单、界面红底高亮，", "size": 8.8, "color": MUTED}],
          [{"text": "但不阻断流水线继续。", "size": 8.8, "color": MUTED}]], line_spacing=1.35)

    text(s, 0.62, 5.54, 5.10, 0.62,
         [[{"text": "冲突是报告，不是失败", "size": 12, "color": ACCENT2, "bold": True}],
          [{"text": "唯一判定：目标格已被占用", "size": 8.5, "color": DIM}]], line_spacing=1.3)

    text(s, 0.62, 6.36, 5.10, 0.30,
         [[{"text": "TINY：59 态，全表 0 冲突", "size": 9, "color": GREEN, "mono": True}]])

    # 右：一处冲突的两个产生式
    section_label(s, 6.14, 1.74, 6.5, "Mini-C：状态 115 × KW_ELSE 这一格")
    cards = [
        ("产生式 31", "if ( E ) statement", "语句体已识别完 → 可以归约", AMBER),
        ("产生式 32", "if ( E ) statement else statement", "下一个符号正是 else → 可以移进", ACCENT2),
    ]
    y = 2.00
    for name, prod, why, col in cards:
        rect(s, 6.14, y, 6.57, 1.10, fill=PANEL, line=LINE, radius=0.02)
        text(s, 6.32, y + 0.12, 1.4, 0.24, [name], size=8.8, color=col, bold=True, mono=True)
        text(s, 6.32, y + 0.42, 6.2, 0.26, [prod], size=10.5, color=TEXT, mono=True)
        text(s, 6.32, y + 0.76, 6.2, 0.24, [why], size=8.3, color=DIM)
        y += 1.22

    rect(s, 6.14, 4.48, 6.57, 1.10, fill=RGBColor(0x2A, 0x1C, 0x1C), line=RGBColor(0x4A, 0x2E, 0x2E), radius=0.02)
    rect(s, 6.14, 4.48, 0.05, 1.10, fill=RED)
    text(s, 6.36, 4.48, 6.2, 1.10,
         [[{"text": "一格两义 → 移进-归约冲突", "size": 11, "color": RED, "bold": True},
           {"text": "　表内保留 Shift(118)", "size": 9.5, "color": MUTED}],
          [{"text": "语义后果：else 就近匹配内层 if，与 C、Java 一致。", "size": 8.8, "color": DIM}]], line_spacing=1.35, anchor="m")

    text(s, 6.14, 5.72, 6.57, 0.90,
         [[{"text": "这不是 LALR 丢了信息，是文法本身二义 —— 它没规定 else 跟哪个 if 结合。", "size": 9, "color": MUTED}],
          [{"text": "规范 LR(1) 在状态 397 报的是同一个冲突，合并只是搬了个位置。", "size": 9, "color": ACCENT2}]], line_spacing=1.45)

    notes(s, "7:40–8:28（48 秒）", "一处冲突，保留 Shift，else 就近",
          ["填表是单趟扫描：非终结符转移写 GOTO，终结符转移写 Shift；转移是 map，(状态, 符号) 唯一，不可能移进-移进。然后遍历每个状态的完成项，按展望符逐格写 Reduce。",
           "所有归约都走同一个 `writeReduce`，这是冲突一个不漏的关键。判定只有一条：目标格已经被占用，就是冲突。移进-归约保留 Shift，归约-归约保留先填的那个。冲突照样进清单、界面红底高亮，也不阻断流水线——冲突是报告，不是失败。",
           "TINY 是 0 冲突。Mini-C 恰好一处：产生式 31 是 if 带一个语句体、已识别完；产生式 32 带 else，下一个符号正是 else。这一格既要按 31 归约，又要移进到 118。这一格在界面上什么样，请看屏幕。"],
          cue="“请看屏幕”说完停半秒再翻页。")
    return s


# ===================================================================== 第 15 页（截图）
def p15(prs):
    s = add_slide(prs)
    shot_head(s, "TASK 02  /  截图 · Mini-C 分析表", 15, "红框 = 状态 115 × KW_ELSE")

    r = hero(s, img("t2_minic_parsetable.png", CROPS["ptable"], up=2), (0.40, 0.62, 12.53, 4.30))
    annotate(s, r, CROPS["ptable"], 0.6092, 0.5382, 0.0350, 0.0335, lw=2.2)

    y = r[1] + r[3] + 0.26
    chips = [("红框 = 状态 115 × KW_ELSE", "表内写的是 s118"), ("31 → 归约 / 32 → 移进", "同一格两种动作")]
    x = 0.40
    for big, small in chips:
        rect(s, x, y, 6.15, 0.62, fill=PANEL, line=LINE, radius=0.02)
        rect(s, x, y, 0.04, 0.62, fill=RED)
        text(s, x + 0.22, y, 5.8, 0.62,
             [[{"text": big, "size": 9.5, "color": RED, "bold": True, "mono": True},
               {"text": "　" + small, "size": 8.5, "color": MUTED}]], anchor="m")
        x += 6.38

    text(s, 0.40, y + 0.86, 12.53, 0.60,
         [[{"text": "这不是 LALR 丢了信息，是文法本身二义 —— 它没规定 else 跟哪个 if 结合。", "size": 9.5, "color": MUTED}],
          [{"text": "规范 LR(1) 在状态 397 报的是同一个冲突，合并只是搬了个位置；保留 Shift 的语义后果是 else 就近匹配内层 if，与 C、Java 一致。", "size": 9.5, "color": ACCENT2}]],
         line_spacing=1.55)

    notes(s, "8:28–8:46（18 秒）", "状态 115 × KW_ELSE，表内写 s118",
          ["就是这一格：状态 115、KW_ELSE 这一列，表里写的是 s118。产生式 31 是 if 带一个语句体、已经识别完；产生式 32 带 else，下一个符号正是 else。这一格既要按 31 归约，又要移进到 118。"],
          cue="此页只放图，停留 18 秒；手指红框，再指下面两条小注。")
    return s


# ===================================================================== 第 16 页（文字）
def p16(prs):
    s = add_slide(prs)
    header(s, "TASK 03  /  端到端验收 · 严格校验", "任务三 · 怎么保证喂进去的是真产物", 16, TOTAL)

    rect(s, 0.62, 1.42, 12.09, 0.82, fill=RGBColor(0x2A, 0x1C, 0x1C), line=RGBColor(0x4A, 0x2E, 0x2E), radius=0.02)
    rect(s, 0.62, 1.42, 0.05, 0.82, fill=RED)
    text(s, 0.86, 1.42, 11.6, 0.82,
         [[{"text": "前置问题　", "size": 9, "color": RED, "bold": True},
           {"text": "任务一留了一条“进程内预览”的降级路径 —— 在任务三是", "size": 12.5, "color": TEXT},
           {"text": "伪证", "size": 12.5, "color": RED, "bold": True},
           {"text": "。", "size": 12.5, "color": TEXT}]], anchor="m")

    gates = [
        ("闸门 1", "复核", "不看返回值，只看 runProducedLex。", "返回值只说明没抛错，不说明产物是真的。", "if (!runProducedLex()) reject"),
        ("闸门 2", "分流", "回退预览 → joint-strict；上游无产物 → joint-lex。", "两种修法完全不同，混成一个错误码就没用了。", "joint-strict  /  joint-lex"),
        ("闸门 3", "双保险 + 闸门", "再查一次词法返回值，不一致即终止。", "strictLexOk_ 不过时，语法流水线一个方法都不调用。", "strictLexOk_ == false → 零调用"),
    ]
    x = 0.62
    for no, head, body, body2, code in gates:
        rect(s, x, 2.44, 3.88, 2.16, fill=PANEL, line=LINE, radius=0.02)
        text(s, x + 0.20, 2.54, 1.2, 0.22, [no], size=8.5, color=RED, bold=True, spacing=60)
        text(s, x + 0.20, 2.78, 3.5, 0.32, [head], size=14, color=TEXT, bold=True)
        text(s, x + 0.20, 3.20, 3.54, 0.96,
             [[{"text": body, "size": 8.8, "color": MUTED}],
              [{"text": body2, "size": 8.8, "color": MUTED}]], line_spacing=1.45)
        text(s, x + 0.20, 4.22, 3.56, 0.26, [code], size=7.8, color=ACCENT2, mono=True)
        if no != "闸门 3":
            arrow(s, x + 3.94, 3.36, 0.16, 0.12, RED)
        x += 4.11

    rows = [
        ["词法产物", "流水线状态", "结局"],
        ["真", "未到 Run", ("自相矛盾 → 拒绝", {"color": AMBER})],
        ["假", "有预览 / 已到 Compile", ("报 joint-strict", {"color": RED, "bold": True})],
        ["假", "停在上游", ("报 joint-lex", {"color": RED, "bold": True})],
    ]
    table(s, 0.62, 4.72, 7.40, [1.32, 2.98, 3.10], rows, row_h=0.38, head_h=0.34,
          size=9, head_size=8.2, align=["c", "l", "l"])

    rect(s, 8.28, 4.72, 4.43, 1.26, fill=PANEL, line=LINE, radius=0.02)
    text(s, 8.46, 4.82, 4.1, 0.24, ["为什么区分两种失败"], size=8.5, color=ACCENT, bold=True)
    text(s, 8.46, 5.10, 4.1, 0.80,
         [[{"text": "joint-strict", "size": 8.3, "color": ACCENT2, "mono": True},
           {"text": "：生成的 C++ 编不过或跑不通", "size": 8.3, "color": MUTED}],
          [{"text": "joint-lex", "size": 8.3, "color": ACCENT2, "mono": True},
           {"text": "：上游根本没产出，查规则或输入", "size": 8.3, "color": MUTED}]], line_spacing=1.4)

    text(s, 0.62, 6.40, 12.09, 0.42,
         [[{"text": "这是“拒绝”，不是“报错”", "size": 10.5, "color": AMBER, "bold": True},
           {"text": " —— 报错可能已污染状态，拒绝保证语法快照三个字段全空，单测因此能断言 token、语法树、产生式三者皆空。", "size": 9.5, "color": MUTED}]], anchor="m")

    notes(s, "8:46–9:42（56 秒）", "预览回退是伪证，三层闸门",
          ["任务三的难点不是重跑前两章，是任务一留了一条降级路径：扫描器编译或运行失败时，词法阶段会用进程内扫描器扫出一份 `sample.lex`。这在任务一是便利，在任务三是伪证——语法分析拿到的不是生成扫描器的产物，而是“看起来一样”的替代品；后面的数字照样成立，报告却已失去意义。",
           "所以联合层做了三层闸门。第一，复核不看返回值，只看 `runProducedLex`——返回值只说明没抛错，不说明产物是真的。第二，分流两种失败：回退的报 `joint-strict`，查生成的 C++；上游没产物的报 `joint-lex`，查规则文件或输入。两者修法完全不同。",
           "第三，复核后再查一次词法返回值，不一致就终止。`strictLexOk_` 不过时，语法流水线的方法一个都不调用。这是“拒绝”不是“报错”：报错可能已污染状态，拒绝保证快照全空，单测因此能断言语法侧三者皆空。"],
          cue="三个闸门逐个指，讲完第三层停一拍，再念底部金句。")
    return s


# ===================================================================== 第 17 页（截图）
def p17(prs):
    s = add_slide(prs)
    shot_head(s, "TASK 03  /  截图 · 词法结果页", 17, "红框 = 来源标注")

    # 局部放大：结果页右上角那行来源标注（原图只有 11px 高，抠出来放大 4 倍）
    rs = place(s, img("t3_lex.png", CROPS["lex_top"], up=4), (8.30, 0.60, 4.63, 0.52),
               border=True, border_color=RGBColor(0x2A, 0x44, 0x5C))
    annotate(s, rs, CROPS["lex_top"], 0.9208, 0.1935, 0.0705, 0.0162, lw=1.4)
    text(s, 0.40, 0.62, 7.60, 0.50,
         [[{"text": "↑ 结果页右上角，局部放大 4 倍", "size": 8.5, "color": DIM}],
          [{"text": "这行不是界面文案，是运行结束后进程写进去的", "size": 9.5, "color": TEXT, "bold": True}]],
         line_spacing=1.35, align="r")

    # 主图：token 表
    rt = place(s, img("t3_lex.png", CROPS["lex_tab"], up=3), (0.40, 1.62, 12.53, 4.60),
               border=True, border_color=RGBColor(0x2A, 0x44, 0x5C))
    cap_at(s, rt, "词法结果页：416 token · 0 ERROR")

    notes(s, "9:42–10:02（20 秒）", "界面上有个伪造不了的出口",
          ["看这一页。右上角这个红框是来源标注，写的是“真实生成扫描器产出”——这不是界面文案，是 `runProducedLex` 的直接渲染。预览回退时，同一位置会变成红字“进程内预览”，语法页保持空白。下面这张表是 416 个 token、0 个 ERROR。"],
          cue="此页只放图，停留 20 秒；先指右上角红框，再指下面表格，最后翻页。")
    return s


# ===================================================================== 第 18 页（文字）
def p18(prs):
    s = add_slide(prs)
    header(s, "TASK 03  /  端到端验收 · 一键联合", "一键联合，960 毫秒", 18, TOTAL,
           sub="两条链在真实进程边界上缝起来：编译并运行生成的扫描器，再用它的产出做语法分析。")

    band = [("35", "规则"), ("165", "NFA 状态"), ("70", "DFA"), ("66", "最小 DFA"),
            ("416", "token · 0 ERROR"), ("78", "产生式"), ("128", "状态"), ("1", "冲突"),
            ("1370", "语法树节点"), ("139", "四元式")]
    x = 0.62
    for i, (num, lab) in enumerate(band):
        hot = i in (4, 7, 9)
        col = AMBER if hot else TEXT
        rect(s, x, 1.78, 1.10, 0.92, fill=PANEL, line=(RGBColor(0x4A, 0x3C, 0x22) if hot else LINE), radius=0.02)
        text(s, x, 1.84, 1.10, 0.42, [num], size=16, color=col, bold=True, mono=True, align="c")
        text(s, x, 2.30, 1.10, 0.30, [lab], size=6.8, color=DIM, align="c")
        if i < len(band) - 1:
            arrow(s, x + 1.105, 2.17, 0.08, 0.09, DIM)
        x += 1.199

    cards = [
        ("两次真实进程", ["一次：编译并运行生成的扫描器 → sample.lex 落盘",
                          "两次：以该文件为输入，跑语法分析 → 树与四元式"]),
        ("960 毫秒", ["从读 .rules 到输出 139 条四元式，含 g++ 编译时间",
                      "16 个阶段全部 ✓ 成功，没有一行“未执行”"]),
        ("覆盖检查", ["9 关键字 / 23 符号 / 73 条非 ε 产生式 全覆盖",
                      "5 条 ε 产生式不产生树节点，不计入分母"]),
    ]
    x = 0.62
    for head, lines in cards:
        rect(s, x, 3.00, 3.88, 1.30, fill=PANEL, line=LINE, radius=0.02)
        text(s, x + 0.20, 3.10, 3.5, 0.28, [head], size=12, color=TEXT, bold=True)
        text(s, x + 0.20, 3.44, 3.54, 0.78,
             [[{"text": lines[0], "size": 8.8, "color": MUTED}],
              [{"text": lines[1], "size": 8.8, "color": MUTED}]], line_spacing=1.45)
        x += 4.11

    rect(s, 0.62, 4.62, 12.09, 0.86, fill=RGBColor(0x14, 0x2C, 0x3E), line=None, radius=0.02)
    rect(s, 0.62, 4.62, 0.05, 0.86, fill=ACCENT)
    text(s, 0.86, 4.62, 11.6, 0.86,
         [[{"text": "产物落盘，不是内存里演算", "size": 11.5, "color": ACCENT2, "bold": True},
           {"text": "　——　联合层验的是真实进程的产物；换一个语言包，这条链一行都不用改。", "size": 9.5, "color": MUTED}]], anchor="m")

    text(s, 0.62, 5.74, 12.09, 0.95,
         [[{"text": "报告是程序渲染的 Markdown，不是手写", "size": 9.5, "color": MUTED},
           {"text": "：16 行阶段时间线、覆盖矩阵、四元式清单都由同一次运行产出，截图即产物。", "size": 9.5, "color": DIM}],
          [{"text": "下面两页是这次运行的两个结果页，请看屏幕。", "size": 9.5, "color": ACCENT2, "bold": True}]],
         line_spacing=1.6)

    notes(s, "10:02–10:44（42 秒）", "960 毫秒跑完整条链",
          ["这是任务三一键联合跑通的实测，全程 960 毫秒。一条链走下来：35 条规则，165 个 NFA 状态，70 个 DFA，最小化到 66；扫出 416 个 token、0 个 ERROR；语法侧 78 条产生式、128 个状态、1 处冲突；建树 1370 个节点；最后 139 条四元式。",
           "这些数字背后是两次真实进程调用：一次编译并运行生成的扫描器，一次用它的产出做语法分析。所以这不是内存里演算出来的，有进程边界、有文件落盘。接下来的结果页，请看屏幕。"],
          cue="念数字带时从左到右扫一遍，不要停在单个数字上；最后一句是翻页信号。")
    return s


# ===================================================================== 第 19 页（截图）
def p19(prs):
    s = add_slide(prs)
    shot_head(s, "TASK 03  /  截图 · 语法结果页", 19, "红框 = 统计行")

    r = hero(s, img("t3_parse.png", CROPS["parse"], up=2))
    annotate(s, r, CROPS["parse"], 0.004, 0.194, 0.332, 0.036, lw=1.8)
    cap_at(s, r, "128 态 · 1 冲突 · 1370 节点 · 139 条四元式")

    notes(s, "10:44–11:02（18 秒）", "128 态 / 1 冲突 / 1370 节点 / 139 条",
          ["这是语法结果页。上面这行统计：78 条产生式、128 个状态、1 处冲突、0 个错误；树是 1370 个节点、417 个叶子、深度 52。下面开始是四元式，一共 139 条。"],
          cue="此页只放图，停留 18 秒；手指红框统计行，再比一下下面的树。")
    return s


# ===================================================================== 第 20 页（文字）
def p20(prs):
    s = add_slide(prs)
    header(s, "TASK 03  /  中间代码", "三件事决定了 IR 的形状", 20, TOTAL,
           sub="四元式，四个字段全是字符串，空串表示这个字段不使用。")

    cards = [
        ("01", "左值路径", "genLValue 只取名字和下标、不读元素；右侧照常求值，最后按有无下标分派。",
         "([]=,1,0,arr)", "与 [] 的 (数组, 下标, 临时变量) 互为镜像"),
        ("02", "名字先行、定义后置", "进 if/while/for 先分配标签名；jfalse 发射时目标已是最终值。",
         "(label,,,L1)", "缓冲只追加、不改写，不需要第二遍扫描"),
        ("03", "操作符归一", "TINY 的相等词素就是等号，不归一成 == 就分不清赋值与比较。",
         "(==,a,b,t15)", "一元与二元运算符共用同一个映射表"),
    ]
    y = 1.78
    for no, head, body, sample, note in cards:
        rect(s, 0.62, y, 7.35, 1.42, fill=PANEL, line=LINE, radius=0.02)
        rect(s, 0.62, y, 0.04, 1.42, fill=ACCENT)
        text(s, 0.84, y + 0.12, 0.4, 0.22, [no], size=9, color=ACCENT, bold=True, mono=True)
        text(s, 1.18, y + 0.10, 5.6, 0.30, [head], size=12.5, color=TEXT, bold=True)
        text(s, 0.84, y + 0.48, 6.95, 0.62, [body], size=9, color=MUTED, line_spacing=1.4)
        text(s, 0.84, y + 1.12, 3.2, 0.24, [sample], size=9, color=ACCENT2, mono=True)
        text(s, 4.00, y + 1.13, 3.85, 0.24, [note], size=7.8, color=DIM, align="r")
        y += 1.54

    # 右：代码块
    rect(s, 8.24, 1.78, 4.47, 2.06, fill=RGBColor(0x0E, 0x1D, 0x2C), line=LINE, radius=0.02)
    text(s, 8.44, 1.88, 4.1, 0.24,
         [[{"text": "if f then g := 1 end", "size": 8, "color": DIM, "mono": True}]])
    text(s, 8.44, 2.20, 4.1, 1.50,
         [[{"text": "(jfalse,f,,L4)", "size": 11, "color": TEXT, "mono": True}],
          [{"text": "(=,1,,g)", "size": 11, "color": TEXT, "mono": True}],
          [{"text": "(label,,,L4)", "size": 11, "color": ACCENT2, "mono": True}]], line_spacing=1.4)

    rect(s, 8.24, 3.98, 4.47, 1.10, fill=PANEL, line=LINE, radius=0.02)
    text(s, 8.44, 4.06, 4.1, 0.94,
         [[{"text": "无 else 分支 → 只分配一个标签", "size": 8.8, "color": TEXT, "bold": True}],
          [{"text": "跳转目标在发射时已确定，所以 IR 一遍成型。", "size": 8.5, "color": DIM}]], line_spacing=1.45)

    text(s, 8.24, 5.24, 4.47, 0.28,
         [[{"text": "四元式字段", "size": 8.5, "color": ACCENT, "bold": True}]])
    text(s, 8.24, 5.52, 4.47, 0.66,
         [[{"text": "op, arg1, arg2, result", "size": 9.5, "color": ACCENT2, "mono": True}],
          [{"text": "编号 = 生成顺序 → 可逐条断言", "size": 8.3, "color": MUTED}]], line_spacing=1.5)

    text(s, 0.62, 6.46, 12.09, 0.30,
         [[{"text": "t17 一定出现在 t16 之后，L4 是最后一个标签", "size": 9, "color": MUTED, "mono": True},
           {"text": " —— 任何一处提前或延后求值，编号错位，断言立刻失败。", "size": 9, "color": DIM}]])

    notes(s, "11:02–11:52（50 秒）", "左值不读元素，标签名字先行",
          ["中间代码是四元式，四个字段全是字符串，空串表示不使用。有三件事决定了它的形状。",
           "第一，赋值必须走左值路径。`variable := expression` 和普通表达式共用一个非终结符，按右值求值，`arr[0] = 1` 会被翻成“先读再赋值”，语义完全错。所以左侧单独走 `genLValue`：只取名字和下标、不读元素；右侧照常生成；最后按有无下标分派成 `=` 或 `[]=`。`[]=` 的字段和 `[]` 正好互为镜像。",
           "第二，回填。困难在于跳转先出现、目标后出现。我用“名字先行、定义后置”：进 if、while、for 时先分配标签名，`jfalse` 发射的瞬间目标就是最终值，`label` 后面才发射。缓冲只追加、不改写，不需要第二遍扫描。",
           "第三，操作符归一。TINY 的相等词素就是等号，不归一成 `==`，四元式里 `(=, a, b, t)` 是赋值还是比较就分不清。具体长什么样，请看屏幕。"],
          cue="三张卡片逐个指，最后一句是翻页信号。")
    return s


# ===================================================================== 第 21 页（截图）
def p21(prs):
    s = add_slide(prs)
    shot_head(s, "TASK 03  /  截图 · 语法树与四元式", 21, "同屏对照")

    r = hero(s, img("t2_minic_treeir.png", CROPS["treeir"], up=2))
    annotate(s, r, CROPS["treeir"], 0.004, 0.194, 0.190, 0.016, lw=1.4)
    cap_at(s, r, "Mini-C：树在上，139 条四元式在下")

    notes(s, "11:52–12:10（18 秒）", "树与四元式同屏，1370 与 139",
          ["这是 Mini-C 的树和四元式同屏。上面这一段是语法树里 statement 往下展开的那几层——`power`、`call`、`arguments` 一路到叶子；下面是四元式，每条四个字段、空串留白。最上面这行统计是 1370 个节点、139 条四元式。"],
          cue="此页只放图，停留 18 秒；先指树，再指下面的四元式。")
    return s


# ===================================================================== 第 22 页（文字）
def p22(prs):
    s = add_slide(prs)
    header(s, "SUMMARY  /  选做与总结", "两项选做，三个结论，六条不足", 22, TOTAL)

    section_label(s, 0.62, 1.36, 4.0, "选做  /  按必做实现")
    cards = [("词法分析源程序生成", ".rules → 700 行独立 C++，零第三方依赖"),
             ("中间代码生成", "TINY 41 条 / Mini-C 139 条，逐条断言")]
    x = 0.62
    for head, body in cards:
        rect(s, x, 1.60, 5.95, 0.74, fill=PANEL, line=LINE, radius=0.02)
        text(s, x + 0.16, 1.66, 5.6, 0.24, [head], size=9.5, color=TEXT, bold=True)
        text(s, x + 0.16, 1.94, 5.7, 0.30, [body], size=8.5, color=MUTED)
        x += 6.14

    section_label(s, 0.62, 2.52, 4.0, "三条结论")
    concl = [("01", "配置驱动不是口号", "两套语言包跑同一份代码；任何硬编码都会在另一套上立刻失败"),
             ("02", "严格性可验证", "三层闸门任一失效，两条回退用例都会红"),
             ("03", "数字自洽", "界面日志（阶段级）+ 报告（汇总级）+ 单测锚点（逐条级）三处一致")]
    y = 2.78
    for no, head, body in concl:
        text(s, 0.62, y, 0.34, 0.24, [no], size=8.5, color=ACCENT, mono=True, bold=True)
        text(s, 1.00, y - 0.02, 2.1, 0.26, [head], size=10, color=ACCENT2, bold=True)
        text(s, 3.16, y, 9.55, 0.26, [body], size=8.5, color=MUTED)
        hline(s, 0.62, y + 0.32, 12.09, LINE, 0.5)
        y += 0.46

    section_label(s, 0.62, 4.32, 4.0, "六条不足  /  已知边界")
    gaps = ["无符号表 / 类型检查", "无错误恢复，只报第一个", "悬垂 else 靠保留 Shift",
            "LALR 路线 B 内存 ×5", "覆盖矩阵只到“可达”", "未压测千行级源程序"]
    x0, y0, w, h = 0.62, 4.58, 3.96, 0.40
    for i, g in enumerate(gaps):
        cx = x0 + (i % 3) * (w + 0.10)
        cy = y0 + (i // 3) * (h + 0.10)
        chip(s, cx, cy, w, h, g, size=8.3, color=MUTED, fill=PANEL, line=LINE)

    text(s, 0.62, 5.54, 12.09, 0.26,
         [[{"text": "另有两处规格偏差：靠用例固化 + 写进报告，让选择可被审查，而不是去改语言定义。", "size": 7.8, "color": DIM}]])

    # 三个独立来源
    rows = [
        ["来源", "粒度", "对应数字"],
        ["界面日志", "阶段级", "每阶段耗时与产物路径"],
        ["Markdown 报告", "汇总级", "416 / 128 / 1 / 1370 / 139"],
        ["单测锚点", "逐条级", "138 项联合流水线断言"],
    ]
    table(s, 0.62, 5.86, 7.40, [2.10, 1.70, 3.60], rows, row_h=0.28, head_h=0.26,
          size=8.2, head_size=7.8, align=["l", "c", "l"])

    section_label(s, 8.30, 5.86, 4.0, "后续顺序")
    steps = ["符号表与类型检查", "基本块划分与活跃分析", "目标代码生成"]
    y = 6.12
    for i, st in enumerate(steps):
        text(s, 8.30, y, 0.36, 0.22, [f"0{i+1}"], size=7.5, color=ACCENT, mono=True)
        text(s, 8.70, y, 3.9, 0.22, [st], size=8.3, color=TEXT)
        y += 0.28

    notes(s, "12:10–13:50（100 秒）", "三个结论，六条不足",
          ["两项选做都做了。第一项是词法分析源程序生成，就是那条链的最后两段，输出不依赖本工程的 C++，700 行上下。第二项是中间代码生成。",
           "补一个细节：139 条四元式是逐条断言的，不是“数量对上就算过”。临时变量和标签的编号本身就是生成顺序的证据——任何一处提前或延后求值，编号都会错位，断言立刻失败。",
           "下面三个结论。第一，配置驱动不是口号。第 3 到第 5 章没有一处“如果是 Mini-C 就怎样”的分支，文法、状态数、冲突位置、四元式模板全部由输入决定。这不是自我克制，是被单测强制的：`parser_pipeline_test` 同时用 TINY 和 Mini-C 跑同一份代码，任何硬编码都会在另一套上立刻失败。",
           "第二，严格性可验证，而不是可声称。三层闸门任何一层失效，那两条回退用例都会红。",
           "第三，数字是自洽的。同一组产物有三个独立来源：界面日志是阶段级，Markdown 报告是汇总级，单测锚点是逐条级。416、128、1、1370、139 在三处一致，才有“实测”的资格。",
           "不足也明确。没有符号表和类型检查，int 和 double 混算不会被发现；没有错误恢复，遇错即停、只报第一个；悬垂 else 靠保留 Shift，语义对齐了但文法仍然二义；LALR 走路线 B，中间态内存约 5 倍；覆盖矩阵只回答“这条产生式有没有被走到”，不回答“循环跑了几次”；最大用例 416 个 token，测不出千行级的性能。",
           "后续如果继续做，顺序应该是：符号表与类型检查、基本块划分与活跃分析、目标代码生成。"],
          cue="这是最长的一页，100 秒；六条不足只指不念全，每条给半句就够。")
    return s


# ===================================================================== 第 23 页
def p23(prs):
    s = add_slide(prs)

    im = Image.open(shot("t0_mainwindow.png")).convert("RGB")
    im = im.resize((int(13.333 * 110), int(7.5 * 110)), Image.LANCZOS)
    im = ImageEnhance.Brightness(im).enhance(1.35)
    im = ImageEnhance.Contrast(im).enhance(1.05)
    im = im.filter(ImageFilter.GaussianBlur(14))
    base = Image.new("RGB", im.size, (0x0B, 0x16, 0x22))
    im = Image.blend(base, im, 0.16)
    buf = io.BytesIO()
    im.save(buf, "PNG")
    buf.seek(0)
    s.shapes.add_picture(buf, Inches(0), Inches(0), Inches(13.333), Inches(7.5))

    text(s, 0.62, 1.02, 8.0, 0.26, ["THANKS  /  结语"], size=9, color=ACCENT, bold=True, spacing=140)
    hline(s, 0.62, 1.42, 12.09, LINE, 0.75)

    text(s, 0.62, 2.10, 12.0, 1.10,
         [[{"text": "规则进，程序出；", "size": 34, "bold": True, "color": TEXT}],
          [{"text": "文法进，分析表出。", "size": 34, "bold": True, "color": ACCENT2}]], line_spacing=1.3)

    hline(s, 0.62, 3.72, 1.5, ACCENT, 1.5)
    text(s, 0.62, 3.94, 8.0, 1.35,
         [[{"text": "9 个模块  ·  1.45 万行源码  ·  0.54 万行单测", "size": 10, "color": MUTED}],
          [{"text": "13/13 CTest  ｜  138 项联合流水线断言  ｜  8 张关键截图整页呈现", "size": 10, "color": MUTED}],
          [{"text": "四个命令行入口：--lex  --parse  --minic  与 GUI", "size": 10, "color": DIM, "mono": True}]],
         line_spacing=1.75)

    text(s, 0.62, 5.62, 8.6, 1.15,
         [[{"text": "感谢老师的指导与各位同学的帮助", "size": 15, "color": TEXT, "bold": True}],
          [{"text": "让“我做完了”变成“你可以验证我做完了”。", "size": 10, "color": MUTED}]], line_spacing=1.5)
    text(s, 9.30, 6.36, 3.41, 0.30, ["谢谢，请批评指正"], size=13, color=ACCENT2, align="r")

    notes(s, "13:50–14:35（45 秒）", "每个数字都可复现",
          ["以上就是我的汇报。十四分钟里，我们走完了这样一条路：一份正则规则变成能独立运行的扫描器，一份 BNF 文法变成分析表和语法树，两条链在真实进程边界上缝起来，最后落到四元式。",
           "我最想说明的不是功能清单，而是每个数字都可以复现——四个命令行入口，13 项单元测试，一条 ctest 命令，就能把报告里的每一张图重新跑出来。这也是我这次最大的收获：让“我做完了”变成“你可以验证我做完了”。",
           "感谢老师在课程设计过程中的指导，也感谢助教和同学在调试阶段给我的帮助。我的汇报到此结束，请老师和各位同学批评指正，谢谢大家。"],
          cue="结尾语速放慢，最后一个字说完停 2 秒再停止录制。")
    return s


# ===================================================================== 附录（隐藏）：一页纸提词卡
def p24(prs):
    s = add_slide(prs)
    header(s, "APPENDIX  /  提词卡（隐藏页）", "一页纸提词卡", 24, TOTAL,
           sub="录制时不必看这一页 —— 每页备注里已带本页金句；本页仅用于打印或贴在屏幕边。")

    rows = [["页", "类型", "只记这一句", "关键数字"]]
    data = [
        ("1", "封面", "题目 + 姓名 + 十四分钟", "—"),
        ("2", "文字", "三个任务一条链，选做按必做", "3 Tab"),
        ("3", "文字", "四层架构，算法层零 Qt", "13 项测试不建 QApplication"),
        ("4", "文字", "9 模块 / 1.45 万行 / 13 全绿", "0.54 万行单测"),
        ("5", "文字", "关键字不进自动机，编码 0 与 255", "26 条 → 18 条 → 121"),
        ("6", "图", "0 号总起点，一行一条边", "121 states"),
        ("7", "文字", "区间压缩 + 自描述 lex + 最长匹配", "52 → 50，700 行"),
        ("8", "图", "ACC[] 就是区间表", "700 行"),
        ("9", "图", "头部登记表 → 自描述", "28 个登记项"),
        ("10", "文字", "两列实测，零错误", "104 / 416"),
        ("11", "文字", "先读 lex 再解析 BNF，化简迭代", "37 条 / 45 符号"),
        ("12", "图", "FOLLOW(statement) 有 else、until", "红框 statement 行"),
        ("13", "文字", "LR(1) 再合并，只增 R/R", "309→59，408→128"),
        ("14", "文字", "一处冲突，保留 Shift", "31 vs 32"),
        ("15", "图", "状态 115 × KW_ELSE 写 s118", "s118"),
        ("16", "文字", "预览回退是伪证，三层闸门", "runProducedLex"),
        ("17", "图", "来源标注伪造不了", "416 token / 0 ERROR"),
        ("18", "文字", "960 毫秒跑完整条链", "416 / 128 / 1 / 1370 / 139"),
        ("19", "图", "统计行四个数", "1370 节点 / 139 条"),
        ("20", "文字", "左值不读元素，标签名字先行", "[]= 与 [] 镜像"),
        ("21", "图", "树在上，四元式在下", "1370 / 139"),
        ("22", "文字", "三个结论，六条不足", "138 项断言"),
        ("23", "文字", "每个数字都可复现", "4 入口 + 1 条 ctest"),
    ]
    rows += [list(d) for d in data]
    table(s, 0.62, 1.62, 12.09, [0.62, 0.82, 6.15, 4.50], rows, row_h=0.204, head_h=0.28,
          size=7.8, head_size=7.8, mono_cols=(0,), align=["c", "c", "l", "l"])
    text(s, 0.62, 6.68, 12.09, 0.24,
         ["注：“图”= 整页截图页，讲完上一页的过渡语后再翻；本页放映时隐藏。"], size=8, color=DIM, align="r")

    s.element.set("show", "0")
    return s


def main():
    prs = Presentation()
    prs.slide_width = Inches(13.333)
    prs.slide_height = Inches(7.5)

    for fn in (p01, p02, p03, p04, p05, p06, p07, p08, p09, p10, p11, p12,
               p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24):
        fn(prs)

    cp = prs.core_properties
    cp.title = "配置驱动的编译器前端自动生成与验证系统"
    cp.subject = "编译原理课程设计 · 14 分半讲解视频（图文分离版）"
    cp.author = "曾理"
    cp.keywords = "编译原理; 词法分析; LALR(1); 四元式; Qt6"

    prs.save(OUT)
    print("saved:", OUT)
    print("slides:", len(prs.slides))


if __name__ == "__main__":
    main()
