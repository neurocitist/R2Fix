# R2Fix

A small KMDF filter that repairs the HID report descriptor of the Satechi R2
Bluetooth Multimedia Remote Control (ST-BTMR2M) for Windows. This is a community
workaround for the affected device, implemented with ChatGPT assistance and
tested on a physical remote.

## Problem and repair

The tested R2 exposes a 336-byte HID report descriptor containing a stray `00`
at byte offset 310. Windows failed to start the HID-over-GATT device with
Code 10. The filter lets the Bluetooth driver retrieve the descriptor, verifies
its length and a nine-byte signature, then changes three bytes in the returned
buffer:

| Offset (zero based) | Original | Repaired |
| --- | --- | --- |
| 310 | `00` | `06` |
| 311 | `05` | `0C` |
| 312 | `0C` | `00` |

The repaired bytes encode Usage Page `0x000C` using a two-byte payload. This
absorbs the stray byte without moving the remaining data or changing the
descriptor's 336-byte length. Other requests are forwarded. A mismatch or
buffer lookup failure preserves the lower driver's completion status and
reported length.

The extension INF attaches the filter only to this hardware ID:

```text
BTHLEDevice\{00001812-0000-1000-8000-00805f9b34fb}_Dev_VID&021915_PID&eeee_REV&0001
```

It appends `R2Fix` to the device's lower filters while retaining the existing
Microsoft `WUDFRd` entry. It does not modify the remote's firmware.

## Validation and scope

Final source revision: **2026100203**. Registry diagnostics, logging work items
and diagnostic counters have been removed.

On 2 October 2026, the final source was built in **Release / x64**, installed
on a Windows 11 workstation, and the remote was reported to work. A loaded
kernel-module inventory identified the R2Fix image, and the SHA-256 hash of its
on-disk file matched the freshly built Release package. 

The INF targets x64. Although the original Visual Studio project includes
ARM64 configurations, ARM64 support has not been implemented and validated in
this package. Different descriptors or remote revisions are not assumed to
need the same correction.

## Build

1. Install Visual Studio 2022 with C++ and WDK integration, plus a compatible
   Windows SDK/WDK. The successful build used WDK `10.0.26100.6584` and the
   `WindowsKernelModeDriver10.0` toolset.
2. Open `R2Fix.sln` and select **Release / x64**.
3. In project properties for that configuration, set Driver Signing to
   **Test Sign** with your own test certificate. Enable Inf2Cat catalog
   generation and **Use Local Time**. Keep the signing private key private.
4. Rebuild. Check that both the SYS and the generated CAT were signed.

The installable output is normally in `x64\Release\R2Fix\`: `R2Fix.inf`,
`R2Fix.sys` and `r2fix.cat`. Export the signing certificate as a public `.cer`
file when transferring that signed package to another computer. The source
archive includes neither binaries nor certificates.

The existing INF warning 1384 recommends declarative filter registration. The
tested device-specific registration is retained unchanged; a migration would
need its own Windows installation tests.

## Install a test-signed package

This version requires **Test Mode** with **Secure Boot disabled**. A Release
build and a trusted public certificate do not make it Microsoft production
signed. **IF YOU DO NOT KNOW WHAT THIS MEANS, DON'T USE THIS SOLUTION.** 

If the destination computer uses BitLocker, save its recovery key and suspend
protection before changing firmware or boot settings. In Administrator
Windows PowerShell:

```powershell
Suspend-BitLocker -MountPoint 'C:' -RebootCount 0 | Out-Null
```

Disable Secure Boot in UEFI, return to Windows, run the following command in
Administrator PowerShell, then restart Windows:

```powershell
bcdedit.exe /set '{current}' testsigning on
```

Copy the three signed package files and the corresponding public certificate
to `C:\R2Fix-Final-Package`. In this example the certificate is named
`R2Fix-Test.cer`. Pair the R2 remote and switch it on, then run:

```powershell
$p = 'C:\R2Fix-Final-Package'
Import-Certificate -FilePath "$p\R2Fix-Test.cer" -CertStoreLocation 'Cert:\LocalMachine\Root' | Out-Null
Import-Certificate -FilePath "$p\R2Fix-Test.cer" -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher' | Out-Null
pnputil.exe /add-driver "$p\R2Fix.inf" /install
```

Record the published `oemNN.inf` name printed by PnPUtil. Restart if requested,
then test the remote's controls. If BitLocker was suspended, resume it after
the boot settings are stable and verify protection is on:

```powershell
Resume-BitLocker -MountPoint 'C:' | Out-Null
Get-BitLockerVolume -MountPoint 'C:' |
    Format-List MountPoint, VolumeStatus, ProtectionStatus
```

Keep Test Mode enabled for this driver. Memory Integrity need not be disabled
merely to enable test signing of a properly signed driver; compatibility with
a particular system still needs testing.

## Uninstall

Use the published INF name for this package on the current computer:

```text
pnputil.exe /delete-driver oemNN.inf /uninstall
```

Complete any requested restart. Before returning to normal boot security, 
uninstall this test-signed filter, disable Test Mode, restore Secure Boot, boot 
Windows, and resume any suspended BitLocker protection. OEM numbers are 
specific to each computer and can be reused.

## Publication and licence

MIT License

Copyright (c) 2026 neurocitist

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

The source and project files were inspected for bundled third-party material
and signing information. They contain no third-party library source, Microsoft
driver binaries, firmware image or signing keys. WDK headers and build tools
are dependencies obtained separately. This file inspection does not establish
the provenance of every line produced in earlier conversations; if an upstream
source is identified, retain its applicable licence and attribution.


## References

- [Windows test-signing requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option)
- [Installing a test certificate](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/installing-a-test-certificate-on-a-test-computer)
- [PnPUtil commands](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax)
- [Driver-signing project properties](https://learn.microsoft.com/en-us/windows-hardware/drivers/develop/driver-signing-properties)
- [Inf2Cat](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/inf2cat)
- [Get-PnpDeviceProperty](https://learn.microsoft.com/en-us/powershell/module/pnpdevice/get-pnpdeviceproperty)
- [HID 1.11 specification](https://www.usb.org/sites/default/files/hid1_11.pdf)
- [Choosing a repository licence](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository)
