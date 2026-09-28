# `wmbus_common`

The wM-Bus decoder behind `wmbus_radio` and `wmbus_meter`, built on [wmbusmeters](https://github.com/wmbusmeters/wmbusmeters).

`drivers/src/*.xmq` are wmbusmeters' driver definitions and `tables/*.yaml` its unit and VIF tables, both verbatim from the ref in `.wmbusmeters_tag`. `drivers/loader/` turns the drivers into C++ tables at codegen time; no upstream C++ is compiled.

## Licence

wmbusmeters is GPL-3.0, and so is this repository (see [LICENSE](../../LICENSE)). Please credit [wmbusmeters](https://wmbusmeters.org/) when you use this.

## Updating upstream

```bash
./scripts/wmbus_common/fetch_upstream.py --diff master   # upstream's C++ changes since the recorded ref
./scripts/wmbus_common/fetch_upstream.py master          # a tag, branch or commit of wmbusmeters
python3 scripts/wmbus_common/check_drivers.py            # which drivers the generator handles
pytest tests/wmbus/host                                  # upstream's test telegrams still decode
```

The fetch commits nothing: review `git diff`, run the checks, then commit or revert.
