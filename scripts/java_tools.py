"""Discover the explicitly installed Java/Gradle tools; no checkout-depth paths."""
from pathlib import Path
import os
import re
import shutil
import subprocess


def java_home():
    selected = os.environ.get('JAVA_HOME') or os.environ.get('JAVA_HOME_17_X64')
    executable = str(Path(selected) / 'bin/java.exe') if selected else shutil.which('java.exe') or shutil.which('java')
    if not executable:
        raise FileNotFoundError('Set JAVA_HOME to JDK 17 or add its java executable to PATH.')
    result = subprocess.run([executable, '-XshowSettings:properties', '-version'], capture_output=True, text=True, check=True)
    text = result.stdout + result.stderr
    version = re.search(r'^\s*java\.specification\.version\s*=\s*(\S+)\s*$', text, re.M)
    home = re.search(r'^\s*java\.home\s*=\s*(.+?)\s*$', text, re.M)
    if not version or version.group(1) != '17' or not home:
        raise ValueError('The selected tool must be a JDK 17 installation.')
    return Path(home.group(1)).resolve()


def gradle_executable():
    home = os.environ.get('GRADLE_HOME')
    candidate = Path(home) / 'bin/gradle.bat' if home else None
    executable = str(candidate) if candidate and candidate.is_file() else shutil.which('gradle.bat') or shutil.which('gradle')
    if not executable:
        raise FileNotFoundError('Set GRADLE_HOME or add Gradle 8.14.4 to PATH.')
    result = subprocess.run([executable, '--version'], capture_output=True, text=True, check=True)
    if not re.search(r'^Gradle 8\.14\.4\s*$', result.stdout, re.M):
        raise ValueError('The receiver build requires Gradle 8.14.4.')
    return executable
