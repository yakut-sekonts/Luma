"""Install pinned compiled models into a generated .app before code signing."""
import argparse
import os
from pathlib import Path
import shutil
import tempfile
from model_assets import verify_artifacts


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("app", type=Path)
    args = parser.parse_args()
    if args.app.is_symlink() or args.app.suffix != ".app" or not (args.app / "Contents/Info.plist").is_file():
        raise SystemExit("Expected a generated application bundle.")
    verify_artifacts(args.source)
    resources = args.app / "Contents/Resources"
    if resources.is_symlink():
        raise SystemExit("Application Resources must not be a symbolic link.")
    resources.mkdir(parents=True, exist_ok=True)
    target = resources / "Models"
    if target.is_symlink() or target.resolve() == args.source.resolve():
        raise SystemExit("Model source and bundle destination must be separate real directories.")
    with tempfile.TemporaryDirectory(prefix=".luma-model-install-", dir=resources) as temporary:
        staged = Path(temporary) / "Models"
        shutil.copytree(args.source, staged)
        verify_artifacts(staged)
        if target.exists():
            shutil.rmtree(target)
        os.rename(staged, target)


if __name__ == "__main__":
    main()
