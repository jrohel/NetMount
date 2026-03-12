# 1.1.0 (2026-03-13)

- **Add support for ABI 2 and bump version to 1.1.0**

    Nmmanage version 1.1.0 extends the "`DRIVE <drive> INFO`" command to
    show whether a drive supports extended features. There are three
    possible states:

    - "Server HAS_EXTENDED_FEATURES: YES"
    - "Server HAS_EXTENDED_FEATURES: NO"
    - "Client does not support extended features"

    Additionally, the "`DRIVE <drive> GET HAS_EXTENDED_FEATURES`" command
    was added. This command reports two possible states:

    - "`YES`" - supported by both client and server
    - "`NO`"  - not supported by client or server

----

# 1.0.0 (2025-11-21)

- First version
