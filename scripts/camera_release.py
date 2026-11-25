#!/usr/bin/env python3
"""Build/verify privados de ESP32-CAM; nunca flashea ni identifica una placa.

Ubuntu: python3 scripts/camera_release.py build --config /privado/deployment_config.h
        --build-dir /privado/release-nuevo
        python3 scripts/camera_release.py verify --build-dir /privado/release-nuevo

La verificación prueba integridad local, no autenticidad de un manifiesto firmado,
reproducibilidad binaria ni correspondencia con el firmware de una placa.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import tempfile

SKETCH = "firmware/esp32cam-evidence/arduino/sd-recovery-firmware"
FQBN = "esp32:esp32:esp32cam"
CORE_VERSION = "3.3.11"
CLI_VERSION = "1.2.2"
REPOSITORY = "https://github.com/khalifa63/seguimiento360-alarma"
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
COMMIT = re.compile(r"(?:[0-9a-f]{40}|[0-9a-f]{64})\Z")


class ReleaseError(Exception):
    """Error seguro: no contiene salidas del compilador ni secretos."""


def run(args, cwd):
    """No propagar stdout/stderr de herramientas: pueden contener configuración."""
    try:
        result = subprocess.run(args, cwd=cwd, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, check=False, timeout=900)
    except (OSError, subprocess.TimeoutExpired):
        raise ReleaseError("No se pudo ejecutar una herramienta requerida.") from None
    if result.returncode:
        raise ReleaseError("Una herramienta requerida falló; salida privada omitida.")
    return result.stdout


def git(repo, *args):
    return run(["git", *args], repo)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inside(path, root):
    return path == root or root in path.parents


def no_symlinks(path):
    """Rechaza también enlaces en ancestros antes de resolver rutas."""
    path = Path(os.path.abspath(path))
    for item in (path, *path.parents):
        if item.is_symlink():
            raise ReleaseError("No se permiten enlaces simbólicos en rutas privadas.")
    return path


def private_path(path, repo):
    path = no_symlinks(path)
    if inside(path.resolve(), repo.resolve()):
        raise ReleaseError("La configuración y el release deben estar fuera del repositorio.")
    return path


def private_mode(path, directory=False):
    if os.name == "posix":
        mode = stat.S_IMODE(path.stat().st_mode)
        if mode & 0o077 or (directory and mode != 0o700):
            raise ReleaseError("La ruta privada requiere directorios 700 y archivos sin acceso ajeno.")


def repository(repo):
    repo = Path(repo).resolve()
    top = Path(os.fsdecode(git(repo, "rev-parse", "--show-toplevel")).strip()).resolve()
    if repo != top:
        raise ReleaseError("--repo debe apuntar a la raíz del repositorio.")
    if git(repo, "status", "--porcelain=v1", "--untracked-files=all", "--ignore-submodules=none").strip():
        raise ReleaseError("Git debe estar limpio, incluidos archivos no versionados.")
    head = git(repo, "rev-parse", "HEAD").decode("ascii").strip()
    if not COMMIT.fullmatch(head):
        raise ReleaseError("HEAD no es un commit completo válido.")
    # Se exige igualdad: no se confía en origin/main local ni en cachés offline.
    remote = git(repo, "ls-remote", "--exit-code", "origin", "refs/heads/main")
    rows = [line.split() for line in remote.decode("ascii").splitlines()]
    if rows != [[head, "refs/heads/main"]]:
        raise ReleaseError("HEAD debe coincidir con main publicado y accesible en origin.")
    return repo, head


def safe_relative(value):
    if not isinstance(value, str) or not value or "\\" in value or ":" in value or "\x00" in value:
        raise ReleaseError("Ruta relativa insegura en manifiesto.")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in value.split("/")):
        raise ReleaseError("Ruta relativa insegura en manifiesto.")
    return path


def source_bytes(repo, commit):
    """Sólo blobs Git regulares del sketch; nunca deployment_config.h."""
    entries = git(repo, "ls-tree", "-rz", commit, "--", SKETCH).split(b"\0")
    result = {}
    for entry in filter(None, entries):
        metadata, raw_path = entry.split(b"\t", 1)
        mode, kind, _oid = metadata.split()
        name = raw_path.decode("utf-8")
        safe_relative(name)
        if mode not in (b"100644", b"100755") or kind != b"blob":
            raise ReleaseError("El sketch no puede contener enlaces ni submódulos.")
        if not name.startswith(SKETCH + "/") or PurePosixPath(name).name == "deployment_config.h":
            raise ReleaseError("Configuración privada versionada o fuente fuera del sketch.")
        content = git(repo, "show", commit + ":" + name)
        source = no_symlinks(repo / name)
        if not source.is_file() or source.read_bytes() != content:
            raise ReleaseError("La fuente local no coincide byte por byte con Git.")
        result[name] = content
    if SKETCH + "/sd-recovery-firmware.ino" not in result:
        raise ReleaseError("Falta el sketch Arduino versionado.")
    return result


def json_object(raw):
    def unique(pairs):
        obj = {}
        for key, value in pairs:
            if key in obj:
                raise ReleaseError("Claves JSON duplicadas.")
            obj[key] = value
        return obj
    try:
        return json.loads(raw, object_pairs_hook=unique)
    except (ValueError, UnicodeError):
        raise ReleaseError("JSON inválido.") from None


def toolchain(repo):
    version = json_object(run(["arduino-cli", "version", "--format", "json"], repo))
    if not isinstance(version, dict) or version.get("VersionString") != CLI_VERSION:
        raise ReleaseError("Se requiere Arduino CLI " + CLI_VERSION)
    cores = json_object(run(["arduino-cli", "core", "list", "--format", "json"], repo))
    platforms = cores.get("platforms", []) if isinstance(cores, dict) else []
    if not isinstance(platforms, list):
        raise ReleaseError("Inventario de cores inválido.")
    if not any(isinstance(p, dict) and p.get("id") == "esp32:esp32"
               and p.get("installed_version") == CORE_VERSION for p in platforms):
        raise ReleaseError("Se requiere el core esp32:esp32 " + CORE_VERSION)


def bin_inventory(root):
    result = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ReleaseError("No se permiten enlaces en el release.")
        if path.is_dir():
            private_mode(path, directory=True)
        elif path.suffix.lower() == ".bin":
            if not path.is_file():
                raise ReleaseError("Artefacto no regular.")
            private_mode(path)
            if path.stat().st_size <= 0:
                raise ReleaseError("Artefacto .bin vacío.")
            result[path.relative_to(root.parent).as_posix()] = {
                "sha256": digest(path.read_bytes()), "size": path.stat().st_size}
    if not result:
        raise ReleaseError("No se encontraron artefactos .bin.")
    return result


def build_release(repo, config, build_dir):
    repo, commit = repository(repo)
    config = private_path(config, repo)
    release = private_path(build_dir, repo)
    if not config.is_file():
        raise ReleaseError("--config debe ser un archivo privado existente.")
    private_mode(config)
    private_mode(config.parent, directory=True)
    if release.exists():
        raise ReleaseError("El directorio de release ya existe; no se sobrescribe.")
    if not release.parent.is_dir():
        raise ReleaseError("El padre privado del release debe existir.")
    private_mode(release.parent, directory=True)
    sources = source_bytes(repo, commit)
    toolchain(repo)
    old_umask = os.umask(0o077)
    try:
        # mkdir exclusivo: tampoco reemplaza un release creado concurrentemente.
        release.mkdir(mode=0o700)
        output = release / "build"
        output.mkdir(mode=0o700)
        with tempfile.TemporaryDirectory(prefix=".camera-source-", dir=release) as temporary:
            staging = Path(temporary) / "sd-recovery-firmware"
            staging.mkdir(mode=0o700)
            for name, content in sources.items():
                target = staging / PurePosixPath(name).relative_to(SKETCH)
                target.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
                target.write_bytes(content)
            shutil.copyfile(config, staging / "deployment_config.h")
            os.chmod(staging / "deployment_config.h", 0o600)
            run(["arduino-cli", "compile", "--fqbn", FQBN,
                 "--build-property", "build.code_debug=3", "--build-property",
                 "build.warn_data_percentage=75", "--build-path", str(output),
                 str(staging)], repo)
        # Arduino puede crear directorios explícitamente: cerrar permisos antes del inventario.
        for path in output.rglob("*"):
            if path.is_symlink():
                raise ReleaseError("La compilación produjo enlaces inesperados.")
            os.chmod(path, 0o700 if path.is_dir() else 0o600)
        artifacts = bin_inventory(output)
        # Detectar cambios concurrentes en HEAD, remoto o fuentes antes de emitir manifiesto.
        if repository(repo)[1] != commit or source_bytes(repo, commit) != sources:
            raise ReleaseError("El repositorio cambió durante la compilación.")
        manifest = {"schema_version": 1, "commit": commit, "fqbn": FQBN,
                    "repository": REPOSITORY,
                    "created_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
                    "core_version": CORE_VERSION, "cli_version": CLI_VERSION,
                    "sketch": SKETCH,
                    "sources": {name: digest(data) for name, data in sources.items()},
                    "artifacts": artifacts}
        with (release / "manifest.json").open("x", encoding="utf-8") as stream:
            json.dump(manifest, stream, indent=2, sort_keys=True)
            stream.write("\n")
        return manifest
    finally:
        os.umask(old_umask)


def verify_release(build_dir, repo):
    """Verifica procedencia local y hashes; NO certifica la placa ni firma el release."""
    repo, commit = repository(repo)
    release = private_path(build_dir, repo)
    if not release.is_dir():
        raise ReleaseError("No existe el release privado.")
    private_mode(release, directory=True)
    manifest_path = no_symlinks(release / "manifest.json")
    private_mode(manifest_path)
    manifest = json_object(manifest_path.read_bytes())
    required = {"schema_version", "commit", "fqbn", "core_version", "cli_version",
                "repository", "created_at", "sketch", "sources", "artifacts"}
    if not isinstance(manifest, dict) or set(manifest) != required:
        raise ReleaseError("Esquema de manifiesto inválido.")
    created_at = manifest["created_at"]
    if not isinstance(created_at, str) or not re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\+00:00", created_at):
        raise ReleaseError("Fecha UTC del manifiesto inválida.")
    try:
        datetime.fromisoformat(created_at)
    except ValueError:
        raise ReleaseError("Fecha UTC del manifiesto inválida.") from None
    if (manifest["repository"] != REPOSITORY
            or type(manifest["schema_version"]) is not int or manifest["schema_version"] != 1
            or manifest["commit"] != commit or manifest["fqbn"] != FQBN
            or manifest["core_version"] != CORE_VERSION or manifest["cli_version"] != CLI_VERSION
            or manifest["sketch"] != SKETCH):
        raise ReleaseError("Identidad o versiones del manifiesto no coinciden.")
    sources = manifest["sources"]
    if not isinstance(sources, dict):
        raise ReleaseError("Fuentes del manifiesto inválidas.")
    for name, value in sources.items():
        safe_relative(name)
        if not isinstance(value, str) or not SHA256.fullmatch(value):
            raise ReleaseError("Hash de fuente inválido.")
    expected = {name: digest(data) for name, data in source_bytes(repo, commit).items()}
    if sources != expected:
        raise ReleaseError("Las fuentes no coinciden con el commit publicado.")
    artifacts = manifest["artifacts"]
    if not isinstance(artifacts, dict) or not artifacts:
        raise ReleaseError("Inventario de artefactos vacío o inválido.")
    for name, entry in artifacts.items():
        path = safe_relative(name)
        if (len(path.parts) < 2 or path.parts[0] != "build" or path.suffix.lower() != ".bin"
                or not isinstance(entry, dict) or set(entry) != {"sha256", "size"}
                or not isinstance(entry["sha256"], str) or not SHA256.fullmatch(entry["sha256"])
                or type(entry["size"]) is not int or entry["size"] <= 0):
            raise ReleaseError("Entrada de artefacto inválida.")
        no_symlinks(release / name)
    output = no_symlinks(release / "build")
    private_mode(output, directory=True)
    if artifacts != bin_inventory(output):
        raise ReleaseError("El inventario o los hashes de artefactos no coinciden.")
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    for command in ("build", "verify"):
        sub = subparsers.add_parser(command)
        sub.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
        sub.add_argument("--build-dir", type=Path, required=True)
        if command == "build":
            sub.add_argument("--config", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "build":
            build_release(args.repo, args.config, args.build_dir)
        else:
            verify_release(args.build_dir, args.repo)
    except (ReleaseError, OSError, UnicodeError) as error:
        # OSError puede incluir rutas privadas: no imprimir su contenido.
        message = str(error) if isinstance(error, ReleaseError) else "Error de acceso a archivos privados."
        print("ERROR: " + message, file=sys.stderr)
        return 1
    print("Integridad local validada. No identifica ni verifica el firmware de ninguna placa.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
