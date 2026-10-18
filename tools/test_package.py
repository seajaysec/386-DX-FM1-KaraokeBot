"""Simulate the update loader, including its power-failure cases."""
import os, subprocess
from pathlib import Path
import fm1pkg_make
root=Path(__file__).resolve().parents[1]
os.chdir(root)
fm1pkg_make.SDK=Path(os.environ.get('AC79_SDK',Path.home()/'fw-AC79_AIoT_SDK'))
old=(root/'build/386dx.bin').read_bytes()[:200000]
loader=(root/'build/loader/ota.bin').read_bytes()
pkg=fm1pkg_make.ufw(fm1pkg_make.flash_image(old,fm1pkg_make.KEY),loader,'FM-1_3860000')
(root/'build/host/old.fwsc').write_bytes(pkg)
subprocess.run(['build/host/ldr_test','build/host/old.fwsc','build/386dx.fwsc'],check=True)
