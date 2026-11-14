#!/usr/bin/env python3
"""Paquetes de fábrica de CAUCESOUND: preparar la carpeta que usa Gradle y mantener el archivo de bloqueo.

  tools/factory_packs.py prepare <carpeta-con-zip> <salida>   Comprueba el SHA-256 de cada ZIP contra el bloqueo y
                                                              descomprime en <salida>/packs/<id>/ (CAUCE_PACKS=<salida>).
  tools/factory_packs.py lock <salida>                        Regenera packs/factory-packs.lock.json a partir de una
                                                              carpeta ya preparada (solo al publicar paquetes nuevos).
La verificación de la compilación la hace Gradle (android/app/build.gradle.kts), sin depender de Python.
"""
import hashlib, json, os, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCK = os.path.join(ROOT, "packs", "factory-packs.lock.json")


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def prepare(zips, out):
    lock = json.load(open(LOCK, encoding="utf-8"))
    dest = os.path.join(out, "packs")
    os.makedirs(dest, exist_ok=True)
    for p in lock["packs"]:
        name = f'{p["id"]}-{p["version"]}.zip'
        z = os.path.join(zips, name)
        if not os.path.isfile(z):
            sys.exit(f"FALTA {name} en {zips} (paquete de fábrica requerido)")
        if sha(z) != p["zipSha256"]:
            sys.exit(f"{name}: SHA-256 distinto del bloqueo ({sha(z)} != {p['zipSha256']})")
        target = os.path.join(dest, p["id"])
        if os.path.exists(target):
            sys.exit(f"{target} ya existe: usa una carpeta de salida vacía")
        with zipfile.ZipFile(z) as zf:
            for n in zf.namelist():
                parts = n.split("/")
                if n.startswith("/") or ".." in parts or parts[0] != p["id"]:
                    sys.exit(f"{name}: entrada inesperada {n!r} (se espera una sola carpeta {p['id']}/)")
            zf.extractall(dest)
        print(f"ok {p['id']} {p['version']}")
    print(f"Listo. Compila con CAUCE_PACKS={os.path.abspath(out)}")


def lock(out, zips=None):
    dest = os.path.join(out, "packs")
    old = {}
    if os.path.isfile(LOCK):
        old = {p["id"]: p for p in json.load(open(LOCK, encoding="utf-8"))["packs"]}
    packs, total = [], 0
    for pid in sorted(os.listdir(dest)):
        mpath = os.path.join(dest, pid, "cauce-pack.json")
        m = json.load(open(mpath, encoding="utf-8"))
        size = sum(f["bytes"] for f in m["files"]) + os.path.getsize(mpath)
        total += size
        entry = {"id": m["id"], "version": m["version"], "license": m["license"], "manifestSha256": sha(mpath),
                 "files": len(m["files"]), "bytes": size, "instruments": [i["id"] for i in m["instruments"]]}
        z = zips and os.path.join(zips, f'{m["id"]}-{m["version"]}.zip')
        if z and os.path.isfile(z):
            entry["zipSha256"] = sha(z)
        elif pid in old and old[pid]["version"] == m["version"] and "zipSha256" in old[pid]:
            entry["zipSha256"] = old[pid]["zipSha256"]
        else:
            sys.exit(f"{pid}: falta el ZIP para calcular zipSha256 (pasa la carpeta de ZIP como tercer argumento)")
        packs.append(entry)
    doc = {"lockVersion": 1,
           "comment": "Paquetes de fábrica que DEBE contener un APK preview/release. Lo comprueba Gradle (verifyFactoryPacks).",
           "maxTotalBytes": int(total * 1.25) if not os.path.isfile(LOCK) else max(json.load(open(LOCK))["maxTotalBytes"], total),
           "packs": packs}
    json.dump(doc, open(LOCK, "w", encoding="utf-8"), indent=2, ensure_ascii=False)
    open(LOCK, "a").write("\n")
    print(f"{LOCK}: {len(packs)} paquetes, {total} bytes")


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 3 and a[0] == "prepare":
        prepare(a[1], a[2])
    elif len(a) in (2, 3) and a[0] == "lock":
        lock(a[1], a[2] if len(a) == 3 else None)
    else:
        sys.exit(__doc__)
