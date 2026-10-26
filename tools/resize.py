import argparse
from pathlib import Path

from PIL import Image

#LLM GENERATED CODE BE AWARE!


def process_image(
    input_path: str,
    output_dir: str | None = None,
    output_path: str | None = None,
    size: int = 320,
    quality: int = 85,
) -> None:
    """
    Изменяет размер изображения до size x size и понижает качество.

    :param input_path: путь к исходному изображению
    :param output_dir: директория для сохранения (по умолчанию рядом с исходным)
    :param output_path: явный путь к файлу (имеет приоритет над output_dir)
    :param size: 320 или 640
    :param quality: качество JPEG/WebP (1-100)
    """
    if size not in (320, 640):
        raise ValueError("Размер должен быть 320 или 640")

    if not (1 <= quality <= 100):
        raise ValueError("Качество должно быть в диапазоне 1-100")

    src = Path(input_path)
    if not src.is_file():
        raise FileNotFoundError(f"Файл не найден: {src}")

    # Определяем итоговый путь
    if output_path is not None:
        dst = Path(output_path)
        dst.parent.mkdir(parents=True, exist_ok=True)
    else:
        base_dir = Path(output_dir) if output_dir else src.parent
        base_dir.mkdir(parents=True, exist_ok=True)
        dst = base_dir / f"{src.stem}_{size}.jpg"

    with Image.open(src) as img:
        if img.mode != "RGB":
            img = img.convert("RGB")

        resized = resize_and_crop_square(img, size)
        resized.save(dst, format="JPEG", quality=quality, optimize=True)

    print(f"Готово: {dst}")


def resize_and_crop_square(img: Image.Image, size: int) -> Image.Image:
    """Масштабирует и обрезает изображение до квадрата size x size."""
    w, h = img.size
    scale = max(size / w, size / h)
    new_w, new_h = int(w * scale + 0.5), int(h * scale + 0.5)
    img = img.resize((new_w, new_h), Image.LANCZOS)

    left = (new_w - size) // 2
    top = (new_h - size) // 2
    return img.crop((left, top, left + size, top + size))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Изменение размера и качества изображения"
    )
    parser.add_argument("input", help="Путь к исходному изображению")
    parser.add_argument(
        "-d",
        "--dir",
        dest="output_dir",
        help="Директория для сохранения результата (создаётся, если нет)",
    )
    parser.add_argument(
        "-o",
        "--output",
        help="Явный путь к итоговому файлу (приоритетнее --dir)",
    )
    parser.add_argument(
        "-s",
        "--size",
        type=int,
        choices=(320, 640),
        default=320,
        help="Размер стороны квадрата: 320 или 640 (по умолчанию 320)",
    )
    parser.add_argument(
        "-q",
        "--quality",
        type=int,
        default=85,
        help="Качество сохранения 1-100 (по умолчанию 85)",
    )
    args = parser.parse_args()

    process_image(
        input_path=args.input,
        output_dir=args.output_dir,
        output_path=args.output,
        size=args.size,
        quality=args.quality,
    )


if __name__ == "__main__":
    main()