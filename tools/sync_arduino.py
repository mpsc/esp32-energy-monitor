# coding: utf-8
"""Sincroniza o espelho do ARDUINO IDE a partir do projeto PlatformIO.

O projeto PlatformIO (src/, include/) e' a fonte de verdade (tem CI).
Este script regenera arduino/ESP32_Energy_Monitor/ para quem preferir usar
a IDE do Arduino:

    - include/*.h                       -> arduino/<sketch>/
    - src/*.h, src/*.cpp (exceto main.cpp) -> arduino/<sketch>/
    - src/main.cpp                       -> arduino/<sketch>/<sketch>.ino
      (a IDE gera prototipos: o 'static' e' removido das FUNCOES; variaveis
       continuam 'static')
    - web_page.h: gerado por tools/gen_web_page.py (rode-o tambem)

Uso:
    python tools/sync_arduino.py
    python tools/sync_arduino.py --check   # so' verifica (nao escreve)

Nao edite o espelho a mao: rode este script apos mudar src/ ou include/.
"""
import argparse
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKETCH = os.path.join(ROOT, "arduino", "ESP32_Energy_Monitor")
SKETCH_NAME = "ESP32_Energy_Monitor"
INO_PATH = os.path.join(SKETCH, SKETCH_NAME + ".ino")
MAIN_CPP = os.path.join(ROOT, "src", "main.cpp")
ANCHOR = "#include <Arduino.h>"


def ler(path):
    with io.open(path, "r", encoding="utf-8") as f:
        return f.read().splitlines()


def escrever(path, linhas):
    with io.open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(linhas) + "\n")


def tira_static(linhas):
    """Remove 'static ' apenas de definicoes de funcao (linhas que abrem '{')."""
    out = []
    for ln in linhas:
        if ln.startswith("static ") and ln.rstrip().endswith("{"):
            ln = ln[len("static "):]
        out.append(ln)
    return out


def gerar_ino():
    """Cabecalho existente do .ino + corpo do main.cpp (sem 'static' nas funcoes)."""
    header = []
    if os.path.exists(INO_PATH):
        for ln in ler(INO_PATH):
            if ln.strip().startswith(ANCHOR):
                break
            header.append(ln)

    cpp = ler(MAIN_CPP)
    inicio = next(i for i, ln in enumerate(cpp) if ln.strip().startswith(ANCHOR))
    corpo = tira_static(cpp[inicio:])
    return header + corpo


def main(argv=None):
    ap = argparse.ArgumentParser(description="Sincroniza o espelho do Arduino IDE.")
    ap.add_argument("--check", action="store_true", help="nao escreve; falha se divergir")
    args = ap.parse_args(argv)

    tarefas = []  # (origem, destino)

    for nome in sorted(os.listdir(os.path.join(ROOT, "include"))):
        if nome.endswith(".h"):
            tarefas.append((os.path.join(ROOT, "include", nome),
                            os.path.join(SKETCH, nome)))

    for nome in sorted(os.listdir(os.path.join(ROOT, "src"))):
        if nome == "main.cpp":
            continue
        if nome.endswith((".h", ".cpp")):
            tarefas.append((os.path.join(ROOT, "src", nome),
                            os.path.join(SKETCH, nome)))

    mudou = []
    for origem, destino in tarefas:
        novo = ler(origem)
        atual = ler(destino) if os.path.exists(destino) else None
        if atual != novo:
            mudou.append(os.path.basename(destino))
            if not args.check:
                escrever(destino, novo)

    novo_ino = gerar_ino()
    atual_ino = ler(INO_PATH) if os.path.exists(INO_PATH) else None
    if atual_ino != novo_ino:
        mudou.append(SKETCH_NAME + ".ino")
        if not args.check:
            escrever(INO_PATH, novo_ino)

    if args.check:
        if mudou:
            print("DIVERGENTE: " + ", ".join(mudou))
            return 1
        print("OK: espelho Arduino em sincronia.")
        return 0

    if mudou:
        print("Sincronizado: " + ", ".join(mudou))
    else:
        print("Nada a fazer: espelho ja' estava em sincronia.")
    print("Lembrete: rode tambem  python tools/gen_web_page.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
