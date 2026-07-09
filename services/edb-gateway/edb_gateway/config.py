import os


def insecure_allowed() -> bool:
    return os.environ.get("EDB_GATEWAY_INSECURE", "") == "1"


HOST = os.environ.get("EDB_GATEWAY_HOST", "127.0.0.1")
PORT = int(os.environ.get("EDB_GATEWAY_PORT", "8765"))
COMMAND_TIMEOUT_S = float(os.environ.get("EDB_GATEWAY_TIMEOUT", "5.0"))
