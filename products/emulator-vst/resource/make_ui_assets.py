#!/usr/bin/env python3
# =============================================================================
# make_ui_assets.py - Arma las imagenes del editor del Emulador
#
# Uso:
#   python3 products/emulator-vst/resource/make_ui_assets.py [img/VU_Meter.png]
#   Requiere Pillow (pip install pillow).
#
# Salida (en esta carpeta; cada imagen en 1x y 2x, la 2x con sufijo _2.0x):
#   vu_face   cuadrante del VU, 349 x 196 (2x: 698 x 392)
#   vu_cap    tapa del pivote, 63 x 23; va encima de la aguja
#   knob      cuerpo del knob, 72 x 72; VSTGUI dibuja encima el arco y el punto
#   panel     fondo del editor, 760 x 424
#
# Campos / logica:
#   Relleno     — el VU original mide 697 x 391; el 2x tiene que ser par para que
#                 el 1x quede exacto (se agrega 1 px transparente a la derecha y abajo)
#   Tapa        — recorte en (282, 300) del 2x; por columna, el borde superior es la
#                 primera fila que se aparta mas de 25 de luminancia del crema de arriba
#                 (filas 280-295). Solo columnas 286-402 (la tapa); el resto queda vacio
#   Borde suave — 1,5 px de transicion de alfa sobre el borde superior de la tapa
#   Knob        — sombra, falda oscura (r 26), cuerpo con degrade (r 23) y cara (r 19);
#                 el arco de valor (r 33) y el punto los pone CKnob desde el uidesc
#   Panel       — degrade vertical con rayado horizontal suave, cabecera de 52 px,
#                 dos pozos detras de los VU y una linea divisoria en y 308.
#                 Las posiciones coinciden con editor.uidesc: si se mueve algo alla,
#                 cambiar PANEL_* aca
#   Dibujo      — todo se dibuja a 4 veces el tamano 2x y se reduce (bordes suaves)
#   Geometria   — pivote, marcas y LED del VU estan en ../VuFace.h;
#                 si cambia el arte hay que volver a medirlos
# =============================================================================

import os
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))

FACE_2X = (698, 392)
CAP_ORIGIN = (282, 300)
CAP_SIZE = (126, 46)
CAP_COLUMNS = (286, 402)

KNOB_SIZE = 72
PANEL_SIZE = (760, 424)
PANEL_HEADER = 52
PANEL_WELLS = [(19, 81, 378, 287), (382, 81, 741, 287)]
PANEL_DIVIDER_Y = 308
PANEL_SCREWS = [(14, 410), (746, 410)]

SUPER = 4


def save_pair(image_2x, name):
    """Guarda la imagen 2x y su version 1x."""
    image_2x.save(os.path.join(HERE, name + "_2.0x.png"))
    half = (image_2x.size[0] // 2, image_2x.size[1] // 2)
    image_2x.resize(half, Image.LANCZOS).save(os.path.join(HERE, name + ".png"))


def luminance(rgb):
    r, g, b = rgb[:3]
    return (r * 299 + g * 587 + b * 114) / 1000.0


def vertical_gradient(size, top, bottom):
    """Imagen RGBA con degrade vertical de top a bottom."""
    mask = Image.linear_gradient("L").resize(size, Image.BILINEAR)
    return Image.composite(Image.new("RGBA", size, bottom), Image.new("RGBA", size, top), mask)


def circle_mask(size, cx, cy, r):
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)
    return mask


# --------------------------------------------------------------------------- VU

def cap_top_edge(face, x):
    """Primera fila (2x) donde empieza la tapa en la columna x."""
    base = sorted(luminance(face.getpixel((x, y))) for y in range(280, 296))[8]
    for y in range(295, 346):
        if abs(luminance(face.getpixel((x, y))) - base) > 25:
            return y
    return None


def make_vu(source):
    original = Image.open(source).convert("RGBA")

    # Cuadrante 2x con relleno transparente
    face = Image.new("RGBA", FACE_2X, (0, 0, 0, 0))
    face.paste(original, (0, 0))
    save_pair(face, "vu_face")

    # Borde superior de la tapa por columna, suavizado con la mediana de 5
    tops = {x: cap_top_edge(face, x) for x in range(CAP_COLUMNS[0], CAP_COLUMNS[1] + 1)}
    smooth = {}
    for x in tops:
        window = [tops[k] for k in range(x - 2, x + 3) if tops.get(k) is not None]
        smooth[x] = sorted(window)[len(window) // 2] if window else None

    # Recorte de la tapa con alfa: opaco debajo del borde, 1,5 px de transicion
    cap = face.crop((CAP_ORIGIN[0], CAP_ORIGIN[1], CAP_ORIGIN[0] + CAP_SIZE[0], CAP_ORIGIN[1] + CAP_SIZE[1]))
    pixels = cap.load()
    for cx in range(CAP_SIZE[0]):
        top = smooth.get(CAP_ORIGIN[0] + cx)
        for cy in range(CAP_SIZE[1]):
            r, g, b, a = pixels[cx, cy]
            alpha = 0.0 if top is None else min(1.0, max(0.0, (CAP_ORIGIN[1] + cy - top + 1.0) / 1.5))
            pixels[cx, cy] = (r, g, b, int(round(a * alpha)))
    save_pair(cap, "vu_cap")


# ------------------------------------------------------------------------- Knob

def make_knob():
    k = SUPER * 2
    size = (KNOB_SIZE * k, KNOB_SIZE * k)
    c = KNOB_SIZE * k / 2.0
    knob = Image.new("RGBA", size, (0, 0, 0, 0))

    # Sombra difusa debajo
    shadow = Image.new("RGBA", size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).ellipse((c - 27 * k, c - 25 * k, c + 27 * k, c + 29 * k), fill=(0, 0, 0, 150))
    knob.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(2.5 * k)))

    # Falda oscura, cuerpo con degrade y cara
    layers = [
        (26, (24, 25, 28, 255), (10, 10, 12, 255)),
        (23, (92, 96, 103, 255), (30, 32, 36, 255)),
        (19, (62, 65, 71, 255), (38, 40, 44, 255)),
    ]
    for radius, top, bottom in layers:
        fill = vertical_gradient(size, top, bottom)
        knob.paste(fill, (0, 0), circle_mask(size, c, c, radius * k))

    # Brillo suave arriba de la cara
    gloss = Image.new("RGBA", size, (0, 0, 0, 0))
    ImageDraw.Draw(gloss).ellipse((c - 14 * k, c - 18 * k, c + 14 * k, c - 4 * k), fill=(255, 255, 255, 28))
    knob.alpha_composite(gloss.filter(ImageFilter.GaussianBlur(3 * k)))

    save_pair(knob.resize((KNOB_SIZE * 2, KNOB_SIZE * 2), Image.LANCZOS), "knob")


# ------------------------------------------------------------------------ Panel

def make_panel():
    k = SUPER * 2
    size = (PANEL_SIZE[0] * k, PANEL_SIZE[1] * k)
    panel = vertical_gradient(size, (38, 41, 45, 255), (21, 23, 26, 255))
    draw = ImageDraw.Draw(panel)

    # Rayado horizontal suave (metal cepillado)
    random.seed(7)
    streaks = Image.new("L", (1, PANEL_SIZE[1] * 2))
    streaks.putdata([random.randint(0, 255) for _ in range(PANEL_SIZE[1] * 2)])
    streaks = streaks.resize(size, Image.BILINEAR)
    panel = Image.composite(Image.new("RGBA", size, (255, 255, 255, 255)), panel,
                            streaks.point(lambda v: v * 6 // 255))
    draw = ImageDraw.Draw(panel)

    # Cabecera mas oscura con filo claro abajo
    draw.rectangle((0, 0, size[0], PANEL_HEADER * k), fill=(16, 17, 19, 255))
    draw.rectangle((0, PANEL_HEADER * k - k, size[0], PANEL_HEADER * k), fill=(6, 6, 7, 255))
    draw.rectangle((0, PANEL_HEADER * k, size[0], PANEL_HEADER * k + k), fill=(56, 59, 64, 255))

    # Pozos detras de los VU: filo claro abajo y sombra interna
    for x0, y0, x1, y1 in PANEL_WELLS:
        box = (x0 * k, y0 * k, x1 * k, y1 * k)
        draw.rounded_rectangle((box[0], box[1] + k, box[2], box[3] + k), radius=7 * k, fill=(62, 65, 70, 255))
        draw.rounded_rectangle(box, radius=7 * k, fill=(9, 10, 11, 255))

    # Linea divisoria entre los VU y los knobs
    y = PANEL_DIVIDER_Y * k
    draw.rectangle((24 * k, y, (PANEL_SIZE[0] - 24) * k, y + k), fill=(10, 11, 12, 255))
    draw.rectangle((24 * k, y + k, (PANEL_SIZE[0] - 24) * k, y + 2 * k), fill=(52, 55, 60, 255))

    # Tornillos
    for sx, sy in PANEL_SCREWS:
        cx, cy, r = sx * k, sy * k, 5 * k
        draw.ellipse((cx - r, cy - r + k, cx + r, cy + r + k), fill=(70, 73, 78, 255))
        screw = vertical_gradient(size, (120, 124, 130, 255), (46, 48, 52, 255))
        panel.paste(screw, (0, 0), circle_mask(size, cx, cy, r))
        draw = ImageDraw.Draw(panel)
        draw.line((cx - 3.2 * k, cy + 1.6 * k, cx + 3.2 * k, cy - 1.6 * k), fill=(22, 23, 25, 255), width=int(1.4 * k))

    save_pair(panel.resize((PANEL_SIZE[0] * 2, PANEL_SIZE[1] * 2), Image.LANCZOS), "panel")


def main():
    source = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "..", "img", "VU_Meter.png")
    make_vu(source)
    make_knob()
    make_panel()
    print("listo:", ", ".join(sorted(f for f in os.listdir(HERE) if f.endswith(".png"))))


if __name__ == "__main__":
    main()
