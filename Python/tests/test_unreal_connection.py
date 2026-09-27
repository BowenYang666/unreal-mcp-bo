import json
import socket
import unittest
from unittest.mock import Mock, patch

import test_grouped_tools


class UnrealConnectionReceiveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = test_grouped_tools.GroupedServerTests().load_server()

    def setUp(self):
        self.connection = self.server.UnrealConnection()
        logger_patch = patch.object(self.server, "logger")
        logger_patch.start()
        self.addCleanup(logger_patch.stop)

    def receive(self, chunks):
        sock = Mock(spec=socket.socket)
        sock.recv.side_effect = chunks
        result = self.connection.receive_full_response(sock)
        sock.settimeout.assert_called_once_with(30)
        return result

    def test_complete_ascii_returns_without_waiting_for_disconnect(self):
        payload = b'{"status":"success","result":{"saved":true}}'
        self.assertEqual(self.receive([payload, AssertionError("Unexpected extra recv")]), payload)

    def test_all_split_positions_preserve_unicode_and_escaped_json(self):
        response = {"status": "success", "result": {
            "label": "MCP\u4e2d\U0001f600\u00e9", "escaped": "quote\" slash\\ newline\n", "values": [1, 2]}}
        for escaped in (False, True):
            payload = json.dumps(response, ensure_ascii=escaped).encode("utf-8")
            for split in range(1, len(payload)):
                with self.subTest(escaped=escaped, split=split):
                    result = self.receive([payload[:split], payload[split:], AssertionError("Unexpected extra recv")])
                    self.assertEqual(result, payload)
                    self.assertEqual(json.loads(result), response)
            self.assertEqual(self.receive([payload[index:index + 1] for index in range(len(payload))]), payload)

    def test_large_response_character_starts_at_byte_4095(self):
        prefix = b'{"status":"success","result":{"label":"'
        label = "x" * (4095 - len(prefix)) + "\u4e2d\U0001f600_" * 2000
        payload = prefix + label.encode("utf-8") + b'"}}'
        self.assertEqual(payload[4095:4098], "\u4e2d".encode("utf-8"))
        chunks = [payload[index:index + 4096] for index in range(0, len(payload), 4096)]
        result = self.receive(chunks)
        self.assertEqual(result, payload)
        self.assertEqual(json.loads(result)["result"]["label"], label)

    def test_invalid_and_truncated_utf8_are_not_ignored(self):
        for chunks in (
            [b'{"label":"\xff', b'"}'],
            [b'{"label":"\xe4', b'\xb8', b''],
            [b'{"label":"\xf0\x9f', b''],
            [b'{}\xe4', b''],
        ):
            with self.subTest(chunks=chunks), self.assertRaises(UnicodeDecodeError):
                self.receive(chunks)

    def test_eof_before_complete_json_raises(self):
        for chunks in ([b''], [b'{"result":', b''], [b'{"label":"end\\', b''], [b'{invalid}', b'']):
            with self.subTest(chunks=chunks), self.assertRaises(ConnectionError):
                self.receive(chunks)

    def test_timeout_or_reset_cannot_return_partial_success(self):
        for partial in (b'{"result":', b'{"label":"\xe4', b'{}\xe4'):
            with self.subTest(partial=partial), self.assertRaisesRegex(TimeoutError, "outcome unknown"):
                self.receive([partial, socket.timeout("timed out")])
        with self.assertRaisesRegex(TimeoutError, "outcome unknown"):
            self.receive([socket.timeout("timed out")])
        with self.assertRaises(ConnectionResetError):
            self.receive([b'{"result":', ConnectionResetError("peer reset")])

    def test_send_command_preserves_fragmented_response_and_does_not_retry(self):
        response = {"status": "success", "result": {"saved": True, "label": "\u4e2d\U0001f600"}}
        payload = json.dumps(response, ensure_ascii=False).encode("utf-8")
        sock = Mock(spec=socket.socket)

        def connect():
            self.connection.socket = sock
            self.connection.connected = True
            return True

        sock.recv.side_effect = [payload[index:index + 1] for index in range(len(payload))]
        with patch.object(self.connection, "connect", side_effect=connect) as connected:
            result = self.connection.send_command("set_asset_properties", {"asset_path": "/Game/Test"})
            self.assertEqual(result, response)
            connected.assert_called_once()
        sock.sendall.assert_called_once()
        sock.close.assert_called_once()
        self.assertIsNone(self.connection.socket)
        self.assertFalse(self.connection.connected)

        sock.reset_mock()
        sock.recv.side_effect = [b'{"status":"success","result":', b'']
        with patch.object(self.connection, "connect", side_effect=connect) as connected:
            result = self.connection.send_command("set_asset_properties", {"asset_path": "/Game/Test"})
            self.assertEqual(result["status"], "error")
            self.assertNotIn("result", result)
            self.assertNotIn("saved", result)
            self.assertNotIn("modified", result)
            connected.assert_called_once()
        sock.sendall.assert_called_once()
        sock.close.assert_called_once()
        self.assertIsNone(self.connection.socket)


if __name__ == "__main__":
    unittest.main()