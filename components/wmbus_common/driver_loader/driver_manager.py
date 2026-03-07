from pathlib import Path

from .driver import CppDriver, Driver, XmqDriver

WMBUS_COMMON_PATH = Path(__file__).parents[1]
LEGACY_DRIVERS_PATH = WMBUS_COMMON_PATH
XMQ_DRIVERS_PATH = WMBUS_COMMON_PATH / "drivers" / "src"


class DriverManager:
    def __init__(self):
        self._all_drivers: dict[str, Driver] = {}
        self._requested_drivers: set[Driver] = set()

    def __add_driver(self, driver: Driver) -> None:
        if driver.name in self._all_drivers:
            raise ValueError(f"Driver with name {driver.name} already exists")
        self._all_drivers[driver.name] = driver

    def load_drivers(self) -> None:
        for p in LEGACY_DRIVERS_PATH.glob("driver_*.cc"):
            self.__add_driver(CppDriver.from_source(p))

        for p in XMQ_DRIVERS_PATH.glob("*.xmq"):
            self.__add_driver(XmqDriver.from_source(p))

    @property
    def available_drivers(self):
        return sorted(self._all_drivers)

    def request_driver(self, driver_name: str):
        driver = self._all_drivers[driver_name]
        self._requested_drivers.add(driver)
        return driver

    def sync_to_directory(self, target_dir: str | Path) -> None:
        target_dir = Path(target_dir)
        target_dir.mkdir(exist_ok=True, parents=True)
        written_files = set()
        for driver in self._requested_drivers:
            target_path = target_dir / f"{driver.name}.cpp"
            old_content = target_path.read_text() if target_path.exists() else ""

            new_content = driver.serialize()
            if old_content != new_content:
                target_path.write_text(new_content)
            written_files.add(target_path)

        for root, dirs, files in target_dir.walk(top_down=False):
            for name in files:
                path = Path(root) / name
                if path not in written_files:
                    path.unlink()
            for name in dirs:
                path = Path(root) / name
                if not any(path.iterdir()):
                    path.rmdir()
