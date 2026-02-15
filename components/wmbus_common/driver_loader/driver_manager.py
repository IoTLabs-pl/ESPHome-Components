from pathlib import Path

from .driver import CppDriver, Driver, XmqDriver
from .field import FieldDefinition, FieldType

WMBUS_COMMON_PATH = Path(__file__).parents[1]
LEGACY_DRIVERS_PATH = WMBUS_COMMON_PATH
XMQ_DRIVERS_PATH = WMBUS_COMMON_PATH / "drivers" / "src"


class DriverManager:
    def __init__(self):
        self.all_drivers: dict[str, Driver] = {}
        self._requested_drivers: dict[Driver, set[FieldDefinition]] = {}

    def add_driver(self, driver: Driver) -> None:
        if driver.name in self.all_drivers:
            raise ValueError(f"Driver with name {driver.name} already exists")
        self.all_drivers[driver.name] = driver

    def load_drivers(self) -> None:
        for p in LEGACY_DRIVERS_PATH.glob("driver_*.cc"):
            self.add_driver(CppDriver.from_source(p))

        for p in XMQ_DRIVERS_PATH.glob("*.xmq"):
            self.add_driver(XmqDriver.from_source(p))

    def request_driver(self, driver_name: str) -> str:
        if driver_name not in self.all_drivers:
            raise ValueError(f"Driver {driver_name} not found")
        driver = self.all_drivers[driver_name]
        self._requested_drivers.setdefault(driver, set())
        return driver_name

    def get_driver(self, driver_name: str) -> Driver:
        return self.all_drivers[driver_name]

    def request_field(
        self, driver_name: str, field_name: str, field_type: FieldType | None = None
    ) -> str:
        driver = self.all_drivers[driver_name]
        matched = driver.request_field(field_name, field_type)
        self._requested_drivers.setdefault(driver, set()).update(matched)
        return field_name

    def sync_to_directory(self, target_dir: str | Path) -> None:
        target_dir = Path(target_dir)
        target_dir.mkdir(exist_ok=True, parents=True)
        written_files = set()
        for driver, requested_fields in self._requested_drivers.items():
            target_path = target_dir / f"{driver.name}.cpp"
            old_content = target_path.read_text() if target_path.exists() else ""

            new_content = driver.serialize(requested_fields or None)
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
