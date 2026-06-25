import logging
import time

logger = logging.getLogger(__name__)

VALID_COMMANDS: set[str] = {
    "POWER_ON",
    "POWER_OFF",
    "SET_MODE",
    "RESET",
    "LOAD_SEQUENCE",
    "EXEC_SEQUENCE",
    "SET_PARAM",
    "NOOP",
}


class CommandHandler:
    def __init__(self) -> None:
        self.stored_queue: list[dict] = []

    def verify(self, cmd: dict) -> tuple[bool, str]:
        command = cmd.get("command", "")
        if command not in VALID_COMMANDS:
            return False, f"Unknown command: {command}"
        if command == "SET_PARAM" and "param" not in cmd:
            return False, "SET_PARAM requires 'param' field"
        if command == "EXEC_SEQUENCE" and "seq_id" not in cmd:
            return False, "EXEC_SEQUENCE requires 'seq_id' field"
        return True, ""

    def execute(self, cmd: dict) -> dict:
        logger.info("Executing command: %s", cmd.get("command"))
        response = {
            "status": "ok",
            "command": cmd.get("command"),
            "timestamp": time.time(),
        }
        if cmd.get("command") == "EXEC_SEQUENCE":
            response["seq_id"] = cmd.get("seq_id")
        return response

    def queue_stored(self, cmd: dict) -> None:
        valid, reason = self.verify(cmd)
        if not valid:
            logger.warning("Cannot queue invalid command: %s", reason)
            return
        cmd["_queued_at"] = time.time()
        self.stored_queue.append(cmd)
        logger.info("Command queued for later execution")
