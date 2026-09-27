from pathlib import Path

from .cpp import TEMPLATES
from .driver import AutoDriver, Driver

XMQ_DRIVERS_PATH = Path(__file__).parents[1] / "src"

GENERATED_DIR = "wmbusmeters_generated"


class DriverManager:
    def __init__(self):
        self._all_drivers: dict[str, Driver] = {}
        self._aliases: dict[str, str] = {}
        self._requested_drivers: set[Driver] = set()
        self._auto_driver = AutoDriver(manager=self)
        self._auto_requested = False

    def load_drivers(self) -> None:
        for p in sorted(XMQ_DRIVERS_PATH.glob("*.xmq")):
            driver = Driver.from_source(p)
            self._all_drivers[driver.name] = driver
            for alias in driver.aliases:
                self._aliases.setdefault(alias, driver.name)

    @property
    def available_drivers(self) -> list[str]:
        return sorted(set(self._all_drivers) | set(self._aliases))

    def request_driver(self, driver_name: str) -> Driver:
        driver = self._all_drivers[self._aliases.get(driver_name, driver_name)]
        self._requested_drivers.add(driver)
        return driver

    @property
    def auto_requested(self) -> bool:
        return self._auto_requested

    def request_auto_driver(self) -> AutoDriver:
        self._auto_requested = True
        return self._auto_driver

    @property
    def registered_drivers(self) -> list[Driver]:
        return sorted(self._requested_drivers)

    def registry_index(self, driver: Driver) -> int:
        return self.registered_drivers.index(driver)

    def sync_to_directory(self, target_dir: str | Path) -> None:
        """Outside src/esphome/, because writer.copy_src_tree() deletes anything there it did not put."""
        if self._auto_requested:
            # An auto meter may bind to any compiled driver, so none can be trimmed.
            for driver in self._requested_drivers:
                driver.requested.clear()

        files = {
            "registry.h": TEMPLATES.get_template("registry.h.j2").render(
                symbols=[d.symbol for d in self.registered_drivers]
            ),
            **{f"{d.name}.cpp": d.serialize() for d in self._requested_drivers},
        }

        target_dir = Path(target_dir)
        target_dir.mkdir(exist_ok=True, parents=True)

        for name, content in files.items():
            path = target_dir / name
            if not path.exists() or path.read_text() != content:
                path.write_text(content)

        # esp-idf globs sources with CONFIGURE_DEPENDS, so stale files would compile.
        for path in target_dir.iterdir():
            if path.name not in files:
                path.unlink()
