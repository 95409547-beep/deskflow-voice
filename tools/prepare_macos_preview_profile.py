#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
# SPDX-License-Identifier: MIT
"""Prepare a private, separate preview profile; never alter the old profile or start apps."""
import argparse
import configparser
import json
import os
from pathlib import Path
import shutil


def read_ini(path):
    values = configparser.RawConfigParser(strict=False)
    values.optionxform = str
    with path.open(encoding='utf-8-sig') as stream:
        values.read_file(stream)
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--voice-source', type=Path, required=True)
    parser.add_argument('--peer', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    destination = args.output.resolve()
    if destination.exists():
        parser.error('Refusing to overwrite an existing profile')
    values = read_ini(args.original.resolve())
    voice = read_ini(args.voice_source.resolve())
    peer = json.loads(args.peer.read_text())
    if not all(peer.get(key) for key in ('host', 'port', 'certificate_sha256', 'token', 'screen')):
        parser.error('A screen-bound private voice pairing is required')
    old_tls = args.original.resolve().parent / 'tls'
    if not old_tls.is_dir() or any(p.is_symlink() for p in old_tls.rglob('*')):
        parser.error('Expected a regular existing TLS directory')
    old_certificate = Path(values.get('security', 'certificate', fallback=str(old_tls / 'deskflow.pem')))
    if not old_certificate.is_absolute() or not old_certificate.is_file():
        parser.error('Original keyboard certificate is missing')
    destination.mkdir(parents=True, mode=0o700)
    os.chmod(destination, 0o700)
    shutil.copytree(old_tls, destination / 'tls')
    certificate = destination / 'tls/deskflow-voice.pem'
    shutil.copy2(old_certificate, certificate)
    for name in ('voice', 'gui', 'core', 'security', 'log'):
        if not values.has_section(name):
            values.add_section(name)
    if voice.has_section('voice'):
        for key, value in voice.items('voice'):
            values.set('voice', key, value)
    private = destination / 'voice'
    private.mkdir(mode=0o700)
    peer_file = private / 'peer.json'
    peer_file.write_text(json.dumps(peer, ensure_ascii=False, indent=2) + '\n')
    values.set('voice', 'peerFile', str(peer_file))
    values.set('voice', 'enabled', 'false')
    values.set('voice', 'shortcut', 'F8')
    values.set('gui', 'startCoreWithGui', 'false')
    values.set('gui', 'autoHide', 'false')
    values.set('gui', 'enableUpdateCheck', 'false')
    values.set('gui', 'closeToTray', 'true')
    values.set('gui', 'logExpanded', 'false')
    values.set('log', 'guiDebug', 'false')
    values.set('security', 'certificate', str(certificate))
    values.set('log', 'toFile', 'false')
    output = destination / 'Deskflow Voice.conf'
    with output.open('w', encoding='utf-8') as stream:
        values.write(stream, space_around_delimiters=False)
    for path in destination.rglob('*'):
        os.chmod(path, 0o700 if path.is_dir() else 0o600)
    print(json.dumps({'profile_prepared': True, 'private_material_printed': False,
                      'auto_core_start': False, 'voice_enabled': False,
                      'old_profile_changed': False, 'activation_required': True}))


if __name__ == '__main__':
    main()
