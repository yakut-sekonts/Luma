"""Compare the actual packaged app identity and installer/archive contents.

This does not replace macOS code signing, notarization or Sparkle verification.
"""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import stat

PREFIX = 'Luma.app/'
BUILD_INFO = PREFIX + 'Contents/Resources/BUILD_INFO.json'
PLIST = PREFIX + 'Contents/Info.plist'
EXECUTABLE = PREFIX + 'Contents/MacOS/Luma'


def bundle_members(archive):
    members = {}
    seen = set()
    for entry in archive.infolist():
        name = entry.filename.rstrip('/')
        path = PurePosixPath(name)
        if (not name or path.is_absolute() or '..' in path.parts
                or str(path) != name or '\\' in name or name in seen):
            raise ValueError('Release ZIP contains an ambiguous or unsafe member path')
        seen.add(name)
        # ditto may include AppleDouble metadata outside the application tree.
        if path.parts[0] == '__MACOSX':
            continue
        if name == 'Luma.app' and entry.is_dir():
            continue
        if not name.startswith(PREFIX):
            raise ValueError('Release ZIP contains an unexpected top-level member')
        kind = stat.S_IFMT(entry.external_attr >> 16)
        if kind not in (0, stat.S_IFREG, stat.S_IFDIR, stat.S_IFLNK):
            raise ValueError('Release ZIP contains a special filesystem entry')
        if not entry.is_dir():
            if kind == stat.S_IFDIR:
                raise ValueError('Release ZIP directory entry has an inconsistent type')
            members[name.removeprefix(PREFIX)] = entry
    return members


def regular_member(members, name, maximum_bytes=None):
    entry = members.get(name.removeprefix(PREFIX))
    if (entry is None or stat.S_IFMT(entry.external_attr >> 16) == stat.S_IFLNK
            or entry.file_size <= 0):
        raise ValueError(f'Release ZIP requires a nonempty regular {name}')
    if maximum_bytes is not None and entry.file_size > maximum_bytes:
        raise ValueError(f'Release metadata is too large: {name}')
    return entry


def verify_zip_identity(archive, release_info):
    members = bundle_members(archive)
    build_entry = regular_member(members, BUILD_INFO, 1024 * 1024)
    plist_entry = regular_member(members, PLIST, 1024 * 1024)
    regular_member(members, EXECUTABLE)
    embedded = json.loads(archive.read(build_entry))
    expected = dict(release_info)
    # Accepted Apple receipts are written after the app has been signed/stapled.
    # All identity, dependency and model fields must still be exactly equal.
    if expected.get('signing') == 'developer-id':
        expected.pop('notarized', None)
        expected.pop('notarization', None)
    if not isinstance(embedded, dict) or embedded != expected:
        raise ValueError('ZIP BUILD_INFO.json differs from the release provenance')
    plist = plistlib.loads(archive.read(plist_entry))
    version = release_info['version']
    if (not isinstance(plist, dict)
            or plist.get('CFBundleShortVersionString') != version
            or plist.get('CFBundleVersion') != version
            or plist.get('CFBundleIdentifier') != 'app.luma.desktop'
            or plist.get('CFBundleExecutable') != 'Luma'):
        raise ValueError('ZIP Info.plist differs from the release app identity')
    return members


def file_digest(stream):
    value = hashlib.sha256()
    while block := stream.read(1024 * 1024):
        value.update(block)
    return value.digest()


def verify_bundle_matches_zip(app, archive, release_info):
    """Compare every regular file and symlink without following directory links."""
    app = Path(app)
    if app.is_symlink() or not app.is_dir():
        raise ValueError('Installer must contain a real application directory')
    expected = verify_zip_identity(archive, release_info)
    actual = {}
    for directory, dirs, files in os.walk(app, followlinks=False):
        for name in dirs + files:
            path = Path(directory) / name
            kind = stat.S_IFMT(path.lstat().st_mode)
            if kind == stat.S_IFDIR:
                continue
            if kind not in (stat.S_IFREG, stat.S_IFLNK):
                raise ValueError('Installer contains a special filesystem entry')
            actual[path.relative_to(app).as_posix()] = path
    if set(actual) != set(expected):
        raise ValueError('Installer and ZIP application members differ')
    total_bytes = 0
    for name, entry in expected.items():
        path = actual[name]
        archive_link = stat.S_IFMT(entry.external_attr >> 16) == stat.S_IFLNK
        if archive_link != path.is_symlink():
            raise ValueError(f'Installer and ZIP entry types differ: {name}')
        if archive_link:
            if entry.file_size > 4096 or archive.read(entry) != os.fsencode(os.readlink(path)):
                raise ValueError(f'Installer and ZIP symlink targets differ: {name}')
        else:
            details = path.stat()
            if (details.st_size != entry.file_size
                    or (details.st_mode & 0o111) != ((entry.external_attr >> 16) & 0o111)):
                raise ValueError(f'Installer and ZIP file metadata differ: {name}')
            with archive.open(entry) as packed, path.open('rb') as installed:
                if file_digest(packed) != file_digest(installed):
                    raise ValueError(f'Installer and ZIP file contents differ: {name}')
            total_bytes += entry.file_size
    return {'files_and_symlinks': len(expected), 'regular_file_bytes': total_bytes,
            'version': release_info['version'], 'source_commit': release_info['source_commit']}
