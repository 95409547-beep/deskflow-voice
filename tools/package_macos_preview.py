#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
# SPDX-License-Identifier: MIT
"""Bundle a built Deskflow Voice preview; never install or start a GUI/core."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--qt-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    dirty = subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=all'], cwd=repo, text=True)
    if dirty.strip():
        parser.error('Commit the reviewed source before packaging')
    sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip()
    source = args.build_dir.resolve() / 'bin/Deskflow.app'
    info = plistlib.loads((source / 'Contents/Info.plist').read_bytes())
    if info.get('CFBundleIdentifier') != 'org.deskflow.voice':
        parser.error('Build with --product; a renamed upstream app is not isolated')
    destination = args.output_dir.resolve()
    if destination.exists():
        parser.error('Use a new output directory; existing candidates are never overwritten')
    destination.mkdir(parents=True)
    app = destination / 'Deskflow Voice.app'
    shutil.copytree(source, app, symlinks=True)
    core = app / 'Contents/MacOS/deskflow-core'
    if not core.is_file():
        parser.error('The matching keyboard core is missing from the bundle')
    subprocess.run([str(args.qt_dir.resolve() / 'bin/macdeployqt'), str(app),
                    '-executable=' + str(core), '-always-overwrite', '-codesign=-'], check=True)
    resources = app / 'Contents/Resources'
    shutil.copy2(repo / 'LICENSE', resources / 'LICENSE')
    shutil.copytree(repo / 'LICENSES', resources / 'LICENSES')
    manifest = {'product': 'Deskflow Voice', 'bundle_id': 'org.deskflow.voice', 'source_commit': sha,
                'version': info.get('CFBundleShortVersionString'), 'preview': True,
                'scope': 'Mac to Windows on the two provisioned computers',
                'includes_private_configuration': False, 'officially_notarized': False}
    (resources / 'deskflow-voice-preview.json').write_text(json.dumps(manifest, indent=2) + '\n')
    subprocess.run(['codesign', '--force', '--deep', '--sign', '-', str(app)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    for executable in (app / 'Contents/MacOS/Deskflow', core):
        subprocess.run([str(executable), '--version'], check=True, capture_output=True)
    manifest['gui_sha256'] = hashlib.sha256((app / 'Contents/MacOS/Deskflow').read_bytes()).hexdigest()
    manifest['core_sha256'] = hashlib.sha256(core.read_bytes()).hexdigest()
    (destination / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n')
    installer = destination / '安装 Deskflow Voice.command'
    shutil.copy2(repo / 'tools/install_macos_preview.command', installer)
    installer.chmod(0o755)
    print(json.dumps({'app': str(app), 'source_commit': sha, 'signature_verified': True}))


if __name__ == '__main__':
    main()
