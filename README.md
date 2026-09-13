# FastDDS dsoftbus Transport

This repository assembles the Fast-DDS and OpenHarmony `communication_dsoftbus`
repositories for development of a dsoftbus transport integration.

## Repository layout

- `Fast-DDS`: Fast DDS downstream source repository.
- `communication_dsoftbus`: OpenHarmony dsoftbus downstream source repository.

The two directories are Git submodules. The top-level repository records the
exact commits used by each integration snapshot.

## Initialize the source tree

```bash
git clone --recurse-submodules git@github.com:Jiusi-pys/FastDDS-dsoftbus-transport.git
```

For an existing checkout:

```bash
git submodule update --init --recursive
```

Update a downstream repository when intentionally changing the integration
snapshot, then commit the updated submodule pointer in this repository:

```bash
git -C Fast-DDS pull
git -C communication_dsoftbus pull
git add Fast-DDS communication_dsoftbus
git commit -m "Update downstream repositories"
```

## Downstream repositories

- Fast DDS: `git@github.com:eProsima/Fast-DDS.git`
- OpenHarmony communication_dsoftbus: `git@gitcode.com:openharmony/communication_dsoftbus.git`

See [README_zh.md](README_zh.md) for the Chinese documentation.
