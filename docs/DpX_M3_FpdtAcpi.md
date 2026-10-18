# DpX v0.5 — ACPI FPDT Pointer Record Parser

The EFI Shell command `DpX.efi -perf-fpdt -input fs0:\\ACPI.FPDT` accepts a **file-backed, full ACPI FPDT table**. It checks the table signature, 36-byte ACPI description header, declared length, complete ACPI checksum, bounded record lengths, Basic Boot and S3 Pointer Record sizes and revisions and rejects duplicate pointers. Vendor/unknown records are counted and skipped.

The Boot Performance Pointer is a **processor-relative physical address**, and is printed solely as an opaque diagnostic value — **not dereferenced**, because unvalidated physical pointers in files must never be followed.

To inspect the FBPT bytes referred to by a pointer when those bytes have been lawfully and independently acquired, use `DpX.efi -perf-parse -input fs0:\\BOOT.FBPT`.

OVMF Shell CI executes both parsers against synthetic valid and malformed FBPT/FPDT files. Live platform ACPI table discovery / memory-map safety validation and event Start/End pairing are still not implemented. See [Development Plan v1.3](DpX_Development_Plan_v1.3.md).
