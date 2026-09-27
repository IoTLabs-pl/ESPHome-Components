"""pytest reads command-line options only from a conftest."""


def pytest_addoption(parser):
    parser.addoption(
        "--no-build", action="store_true", help="reuse the last tests/wmbus/host build"
    )
