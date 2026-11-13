# Reference source and license notes

This port was assembled from source snapshots; it does not copy CrossPoint or CrossInk application source.

- Folio Nooir v1.6.6: official tag in the upstream Folio Nooir repository; see `.provenance/folio-nooir-1.6.6-source.zip` and `folio-nooir-1.6.6-source.zip.sha256`.
- Base FreeInk SDK snapshot: local exact commit recorded in `.provenance/freeink-sdk-base.txt`; its MIT license and NOTICE are present at `freeink-sdk/`.
- Experimental S3 FreeInk SDK snapshot: exact upstream commit recorded in `.provenance/freeink-sdk-s3.txt`; MIT license and NOTICE are present at `freeink-sdk-s3/`.
- Native simulator dependency: upstream CrossPoint Simulator revision `4c78f3ff51b5b1e5d2f69f595dba79bc159c4c1f`, resolved by PlatformIO when the simulator environments are built.
- The emulator/QEMU project was not copied into this source tree. Any later QEMU distribution must follow its upstream GPL-2.0 terms and include the corresponding notices/source offer.

All third-party notices already present in the two SDK snapshots remain in place. An exported firmware binary, if produced, must be reviewed against all transitive library licenses before redistribution.
