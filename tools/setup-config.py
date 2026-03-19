#!/usr/bin/env python3
"""
setup-config.py

Generates include/config.h from include/config.h.template if it does not
exist yet. Prompts the user for WiFi credentials and optional mDNS hostname.

Usage:
    python3 tools/setup-config.py
"""

import sys
import os

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE  = os.path.join(REPO_ROOT, "include", "config.h.template")
OUTPUT    = os.path.join(REPO_ROOT, "include", "config.h")


def prompt(label: str, default: str = "") -> str:
    if default:
        value = input(f"  {label} [{default}]: ").strip()
        return value if value else default
    while True:
        value = input(f"  {label}: ").strip()
        if value:
            return value
        print("  This field is required.")


def main() -> None:
    if os.path.exists(OUTPUT):
        print(f"[setup-config] {OUTPUT} already exists — skipping setup.")
        return

    if not os.path.exists(TEMPLATE):
        print(f"[setup-config] Template not found: {TEMPLATE}", file=sys.stderr)
        sys.exit(1)

    print()
    print("=" * 60)
    print("  OctoPrintCam — first-time configuration")
    print("=" * 60)
    print()

    ssid      = prompt("WiFi SSID")
    password  = prompt("WiFi password")
    mdns_name = prompt("mDNS hostname (device will be reachable as <name>.local)", "octocam")

    with open(TEMPLATE) as f:
        content = f.read()

    content = content.replace('"YOUR_WIFI_SSID"',     f'"{ssid}"')
    content = content.replace('"YOUR_WIFI_PASSWORD"', f'"{password}"')
    content = content.replace('"octocam"',             f'"{mdns_name}"')

    with open(OUTPUT, "w") as f:
        f.write(content)

    print()
    print(f"[setup-config] Written to {OUTPUT}")
    print(f"[setup-config] Device hostname: http://{mdns_name}.local")
    print()


if __name__ == "__main__":
    main()
