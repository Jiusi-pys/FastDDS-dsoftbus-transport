# Cross-board device suite

`dsb_suite` runs one Fast DDS feature, QoS, Security and Statistics suite between two OpenHarmony boards over the DSoftBus Transport. The same source builds against Fast DDS 2.14.5, 3.2.5 and 3.6.2; `compat.hpp` holds the API differences. `cft_race` reproduces the Fast DDS content-filter race with the stock UDPv4/TCPv4 transports.

Build with `scripts/release/build_transport_ohos.sh`. Deploy and run with `scripts/release/run_cross_board.py`. See `docs/FASTDDS_VERSIONS_AND_RELEASE.md`.
