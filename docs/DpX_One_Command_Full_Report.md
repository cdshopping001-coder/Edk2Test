# DpX 0.12.0 — One-command complete DUT inspection

## Primary command

```
Shell> DpX.efi
```

No ROM or FBPT files required. The default operation runs **six sequential report sections**: (1) local system info, (2) ACPI FPDT, memory-map-checked FBPT, Extended FPDT Start/End pair analysis with top inclusive intervals and GUID→FFS UI names, (3) FV2 and FVB physical FV/FFS sections, standard decompression and nested FVs when safely supported, (4) full locally visible FFS GUID→UI module catalog, (5) read-only SPI/FVB interface capability inventory, and (6) source/coverage status. Unknown data remain N/A, no writes or reboots.

### Measurement & attribution guarantees

- End timestamp minus Start timestamp in the Extended FPDT record, **nanoseconds**. Pairs must match record type, Progress ID class, APIC source, full 128-bit logging GUID, second Trigger GUID and exact token. Concurrent/unclosed/mismatched events are not combined.
- For Callback Dual-GUID records, **Guid1 identifies the logging module**, **Guid2 is an Event/Protocol/PPI trigger**, *not necessarily another module*. A module label is resolved only from a concrete FV2 FFS GUID match; duplicate matches are AMBIGUOUS. Token may contain `__func__` from EDK2 instrumentation, but is labelled **UNVERIFIED_TOKEN**, not an authenticated MAP/PDB symbol.
- Only inclusive intervals are reported. A token does not establish a complete execution call tree, and nested time must not be added into a parent total.
- FV Used/Free is on-flash byte accounting. Decompressed FFS and nested FVs add **logical children** only, not physical bytes to the parent FV. GUID-defined vendor codecs without a safe supported decoder are N/A.
- Shell will not contain future Windows OS loader/ExitBootServices timestamps. Current boot may lack FPDT or selected event logging. Empty sections are identified as unavailable.

### Functional regression

The OVMF Shell CI executes no-argument DpX and `-summary`, as well as a synthetic `EVENT.FBPT` containing `PERF_CALLBACK_START_ID(0x20)` and `PERF_CALLBACK_END_ID(0x21)` for **PciBusDxe FFS GUID**, a different Trigger GUID, the token PciEnumerator, and timestamps 1,000,000,000 → 1,120,000,000 ns. Expected inclusive interval: **120,000,000 ns**, Module `PciBusDxe`, `FunctionSymbol=UNVERIFIED_TOKEN`.

### Remaining external gates

The version does **not** guarantee function names without proper MAP/PDB/build-identity symbols or reliable FPDT token, nor direct Intel PCH SPI access when firmware provides no safe read-only protocol. Multi-boot persistence and Citadel/NVLS/NBLS hardware tests remain separate acceptance work; OVMF tests cannot substitute for platform-specific validation.
