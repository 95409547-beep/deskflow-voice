#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Voice contributors
# SPDX-License-Identifier: MIT
"""Build isolated development binaries. Never install or start an application."""
import argparse
import os
from pathlib import Path
import platform
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qt-dir', type=Path, required=True)
    parser.add_argument('--openssl-dir', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, default=Path.home() / 'Library/Caches/DeskflowVoice/build/macos')
    parser.add_argument('--deployment-target', default='15.0')
    parser.add_argument('--test', action='store_true')
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('This entry builds macOS; use the portable voice-core entry for other hosts')
    repo = Path(__file__).resolve().parents[1]
    tools = Path(sys.executable).parent
    cmake = tools / 'cmake'
    ctest = tools / 'ctest'
    ninja = tools / 'ninja'
    if not all(p.is_file() for p in (cmake, ctest, ninja)):
        parser.error('Run with the build-tools virtual environment from docs/build/macos.md')
    qt = args.qt_dir.resolve()
    ssl = args.openssl_dir.resolve()
    if not (qt / 'lib/cmake/Qt6/Qt6Config.cmake').is_file():
        parser.error('Qt SDK is incomplete')
    version = subprocess.check_output([str(qt / 'bin/qmake'), '-query', 'QT_VERSION'], text=True).strip()
    if version != '6.11.3':
        parser.error('This foundation is pinned to Qt 6.11.3')
    if not (ssl / 'lib/libssl.a').is_file() or not (ssl / 'include/openssl/ssl.h').is_file():
        parser.error('Provide a complete static OpenSSL SDK built for the selected macOS target')
    env = os.environ.copy()
    env['PATH'] = str(qt / 'bin') + os.pathsep + str(tools) + os.pathsep + env.get('PATH', '')
    configure = [str(cmake), '-S', str(repo), '-B', str(args.build_dir.resolve()), '-G', 'Ninja',
                 '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_INSTALLER=OFF', '-DBUILD_TESTS=ON',
                 '-DSKIP_BUILD_TESTS=ON', '-DCMAKE_PREFIX_PATH=' + str(qt),
                 '-DCMAKE_MAKE_PROGRAM=' + str(ninja), '-DOPENSSL_ROOT_DIR=' + str(ssl),
                 '-DOPENSSL_USE_STATIC_LIBS=ON', '-DCMAKE_OSX_DEPLOYMENT_TARGET=' + args.deployment_target,
                 '-DCMAKE_OSX_ARCHITECTURES=' + platform.machine()]
    subprocess.run(configure, env=env, check=True)
    subprocess.run([str(cmake), '--build', str(args.build_dir), '--parallel', str(min(os.cpu_count() or 2, 8))],
                   env=env, check=True)
    if args.test:
        # Explicit safe suite excludes upstream cases that post real OS keys.
        env['QT_QPA_PLATFORM'] = 'offscreen'
        subprocess.run([str(ctest), '--test-dir', str(args.build_dir / 'src/unittests'),
                        '-R', '^(VoiceSessionTests|VoiceSettingsTests|VoiceBackendTests|OSXKeyStateSafeTests|TISMainThreadProbe|TISReadPathsTests|TISQtRuntimeProbe)$',
                        '--output-on-failure'], env=env, check=True)


if __name__ == '__main__':
    main()
