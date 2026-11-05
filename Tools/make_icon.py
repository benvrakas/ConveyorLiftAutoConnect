"""Draws the Conveyor Lift Auto-Connect Plus icon: a blueprint floor being placed above a built floor, with the lifts
the mod generates between them, seen from the side.

  python Tools/make_icon.py

Writes Docs/Images/icon-1080.png (full resolution), Docs/Images/icon-512.png (mod page) and Resources/Icon128.png
(shown in the game's Mods menu). The design is laid out on a 512-unit grid and rendered at full resolution.
"""
import math
import os
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZE = 512  # Design grid; all coordinates below are in these units.
FULL_SIZE = 1080
SUPERSAMPLE = 2  # Drawn larger than FULL_SIZE and scaled down for smooth edges.
SCALE = FULL_SIZE * SUPERSAMPLE / SIZE

BACKGROUND_TOP = (38, 44, 54)
BACKGROUND_BOTTOM = (22, 26, 32)
FLOOR = (150, 154, 160)
FLOOR_EDGE = (98, 102, 110)
HOLE = (30, 34, 40)
BLUEPRINT = (64, 132, 214)
BLUEPRINT_EDGE = (190, 222, 255)
ORANGE = (250, 149, 73)
ORANGE_DARK = (196, 104, 40)
BRIDGE = (255, 206, 140)
BRIDGE_DARK = (232, 146, 66)
WHITE = (244, 246, 248)

LIFT_X = (172, 340)  # Lift centres.
LIFT_WIDTH = 54
HOLE_WIDTH = 78
UPPER_FLOOR = (138, 180)  # Top and bottom of the blueprint floor.
LOWER_FLOOR = (330, 372)  # Top and bottom of the built floor.
FLOOR_SPAN = (52, 460)


def s(value):
    return value * SCALE


def box(x0, y0, x1, y1):
    return [s(x0), s(y0), s(x1), s(y1)]


def dashed_rect(draw, x0, y0, x1, y1, color, width, dash, gap):
    for (ax, ay), (bx, by) in [((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))]:
        length = math.hypot(bx - ax, by - ay)
        pos = 0.0
        while pos < length:
            end = min(pos + dash, length)
            draw.line([(s(ax + (bx - ax) * pos / length), s(ay + (by - ay) * pos / length)),
                       (s(ax + (bx - ax) * end / length), s(ay + (by - ay) * end / length))], fill=color, width=int(s(width)))
            pos = end + gap


def lift(draw, cx, y0, y1, fill, slat):
    """A conveyor lift segment: a column with horizontal belt slats."""
    half = LIFT_WIDTH / 2
    draw.rectangle(box(cx - half, y0, cx + half, y1), fill=fill)
    y = y0 + 12
    while y < y1 - 6:
        draw.line([(s(cx - half + 8), s(y)), (s(cx + half - 8), s(y))], fill=slat, width=int(s(5)))
        y += 22
    draw.line([(s(cx - half), s(y0)), (s(cx - half), s(y1))], fill=slat, width=int(s(4)))
    draw.line([(s(cx + half), s(y0)), (s(cx + half), s(y1))], fill=slat, width=int(s(4)))


def chevron(draw, cx, cy, up, color):
    d = -1 if up else 1
    points = [(cx - 17, cy - 9 * d), (cx, cy + 9 * d), (cx + 17, cy - 9 * d)]
    draw.line([(s(x), s(y)) for x, y in points], fill=color, width=int(s(9)), joint="curve")


def floor_pieces(top, bottom):
    """The floor between its holes, as boxes in design units."""
    edges = [FLOOR_SPAN[0]]
    for cx in LIFT_X:
        edges += [cx - HOLE_WIDTH / 2, cx + HOLE_WIDTH / 2]
    edges.append(FLOOR_SPAN[1])
    return [(edges[i], top, edges[i + 1], bottom) for i in range(0, len(edges), 2)]


def hole_rims(draw, top, bottom, color):
    """Dark gaps between each lift and the sides of its floor hole."""
    for cx in LIFT_X:
        draw.rectangle(box(cx - HOLE_WIDTH / 2, top, cx - LIFT_WIDTH / 2, bottom), fill=color)
        draw.rectangle(box(cx + LIFT_WIDTH / 2, top, cx + HOLE_WIDTH / 2, bottom), fill=color)


def main():
    big = int(FULL_SIZE * SUPERSAMPLE)
    image = Image.new("RGB", (big, big))
    draw = ImageDraw.Draw(image)
    for row in range(big):
        t = row / big
        color = tuple(int(BACKGROUND_TOP[i] + (BACKGROUND_BOTTOM[i] - BACKGROUND_TOP[i]) * t) for i in range(3))
        draw.line([(0, row), (big, row)], fill=color)

    # Glow behind the generated lifts.
    glow = Image.new("RGBA", image.size, (0, 0, 0, 0))
    glow_draw = ImageDraw.Draw(glow)
    for cx in LIFT_X:
        glow_draw.rectangle(box(cx - LIFT_WIDTH / 2 - 10, UPPER_FLOOR[1], cx + LIFT_WIDTH / 2 + 10, LOWER_FLOOR[0]),
                            fill=BRIDGE + (150,))
    glow = glow.filter(ImageFilter.GaussianBlur(s(14)))
    image.paste(glow, (0, 0), glow)
    draw = ImageDraw.Draw(image)

    # Lifts already in the blueprint above and in the world below, running through their floor holes.
    for cx in LIFT_X:
        lift(draw, cx, 34, UPPER_FLOOR[1], ORANGE, ORANGE_DARK)
        lift(draw, cx, LOWER_FLOOR[0], 486, ORANGE, ORANGE_DARK)

    # The generated lifts in the gap, brighter, with their flow.
    for cx, up in zip(LIFT_X, (True, False)):
        lift(draw, cx, UPPER_FLOOR[1], LOWER_FLOOR[0], BRIDGE, BRIDGE_DARK)
        for cy in (214, 256, 298):
            chevron(draw, cx, cy, up, WHITE)

    # The built floor.
    for piece in floor_pieces(*LOWER_FLOOR):
        draw.rectangle(box(*piece), fill=FLOOR)
        draw.line([(s(piece[0]), s(piece[1])), (s(piece[2]), s(piece[1]))], fill=FLOOR_EDGE, width=int(s(4)))
    hole_rims(draw, *LOWER_FLOOR, HOLE)

    # The blueprint floor being placed: translucent blue with a dashed outline.
    overlay = Image.new("RGBA", image.size, (0, 0, 0, 0))
    overlay_draw = ImageDraw.Draw(overlay)
    for piece in floor_pieces(*UPPER_FLOOR):
        overlay_draw.rectangle(box(*piece), fill=BLUEPRINT + (215,))
    hole_rims(overlay_draw, *UPPER_FLOOR, HOLE + (170,))
    image.paste(overlay, (0, 0), overlay)
    draw = ImageDraw.Draw(image)
    dashed_rect(draw, FLOOR_SPAN[0], UPPER_FLOOR[0], FLOOR_SPAN[1], UPPER_FLOOR[1], BLUEPRINT_EDGE, 4, 16, 10)

    # "Plus" badge.
    bx, by, r = 432, 84, 50
    draw.ellipse(box(bx - r, by - r, bx + r, by + r), fill=ORANGE, outline=ORANGE_DARK, width=int(s(6)))
    draw.rectangle(box(bx - 26, by - 7, bx + 26, by + 7), fill=WHITE)
    draw.rectangle(box(bx - 7, by - 26, bx + 7, by + 26), fill=WHITE)

    full = image.resize((FULL_SIZE, FULL_SIZE), Image.LANCZOS)
    docs = os.path.join(ROOT, "Docs", "Images")
    resources = os.path.join(ROOT, "Resources")
    os.makedirs(docs, exist_ok=True)
    os.makedirs(resources, exist_ok=True)
    full.save(os.path.join(docs, "icon-1080.png"))
    full.resize((512, 512), Image.LANCZOS).save(os.path.join(docs, "icon-512.png"))
    full.resize((128, 128), Image.LANCZOS).save(os.path.join(resources, "Icon128.png"))
    print("Wrote Docs/Images/icon-1080.png, Docs/Images/icon-512.png and Resources/Icon128.png")


if __name__ == "__main__":
    main()
