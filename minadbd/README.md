minadbd is now mostly built from libadbd. The fuse features are unique to
minadbd, and services.c has been modified as follows:

  - all services removed
  - all host mode support removed
  - `sideload_service()` added; this is the only service supported. It
    receives a single blob of data, writes it to a fixed filename, and
    makes the process exit.
