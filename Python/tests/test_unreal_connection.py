import json
import asyncio
import socket
import os
import unittest
from concurrent.futures import ThreadPoolExecutor
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

    def test_bound_requests_require_native_identity_without_fallback(self):
        expected = self.server.os.path.normcase(self.server.os.path.normpath("C:/Projects/NF/NF.uproject"))
        arguments = {"asset_path": "/Game/DA_Test"}
        for binding, success in (({"contract": 1, "project_path": expected, "matched": True}, True),
                                 ({}, False),
                                 ({"contract": 1, "project_path": "C:/Projects/SkillTest/SkillTest.uproject", "matched": True}, False)):
            with self.subTest(binding=binding), patch.object(self.server, "UNREAL_EXPECTED_PROJECT", expected):
                response = {"status": "success", "result": {"saved": True}, "project_binding": binding}
                sock = Mock(spec=socket.socket); sock.recv.return_value = json.dumps(response).encode()
                def connect():
                    self.connection.socket = sock
                    return True
                with patch.object(self.connection, "connect", side_effect=connect) as connected:
                    result = self.connection.send_command("set_asset_properties", arguments)
                self.assertEqual(result["status"], "success" if success else "error")
                if not success:
                    self.assertNotIn("result", result)
                    self.assertIn("outcome unknown", result["error"])
                self.assertEqual(json.loads(sock.sendall.call_args.args[0]), {"type": "execute_project_command", "params": {
                    "expected_project": expected, "command": "set_asset_properties", "arguments": arguments}})
                connected.assert_called_once(); sock.sendall.assert_called_once(); sock.close.assert_called_once()

    def test_service_lifespan_does_not_reserve_an_idle_editor_socket(self):
        async def run():
            with patch.object(self.server, "_unreal_connection", None), patch.object(self.server, "get_unreal_connection") as factory:
                async with self.server.server_lifespan(self.server.mcp):
                    factory.assert_not_called()
                factory.assert_not_called()
        asyncio.run(run())

    def test_project_profile_validation(self):
        for value in ("relative/NF.uproject", "C:/Projects/NF", "C:/Projects/NF.txt", "C:/" + "x" * 4096 + ".uproject"):
            with self.subTest(value=value), patch.dict(os.environ, {"UNREAL_MCP_EXPECTED_PROJECT": value}), self.assertRaises(ValueError):
                test_grouped_tools.GroupedServerTests().load_server()

    def test_two_profiles_use_separate_real_sockets_and_refuse_wrong_target(self):
        projects = [self.server.os.path.normcase(self.server.os.path.normpath(path)) for path in
                    ("C:/Projects/NF/NF.uproject", "C:/Projects/SkillTest/SkillTest.uproject")]
        listeners = [socket.socket(socket.AF_INET, socket.SOCK_STREAM) for _ in projects]
        for listener in listeners:
            self.addCleanup(listener.close); listener.bind(("127.0.0.1", 0)); listener.listen(3); listener.settimeout(8)
        def serve(listener, project):
            requests = []
            for request_index in range(2):
                peer, _ = listener.accept()
                with peer:
                    peer.settimeout(5); data = b""
                    while True:
                        chunk = peer.recv(4096)
                        if not chunk: raise ConnectionError("Incomplete test request")
                        data += chunk
                        try:
                            request = json.loads(data); break
                        except json.JSONDecodeError:
                            continue
                    requests.append(request); self.assertEqual(request["type"], "execute_project_command")
                    matched = request["params"]["expected_project"] == project
                    response = {"status": "success" if matched else "error", "project_binding": {
                        "contract": 1, "project_path": project, "matched": matched}}
                    response["result"] = {"project": project, "asset_path": request["params"]["arguments"].get("asset_path")} if matched else {
                        "success": False, "stage": "project_binding", "execution_state": "not_started", "modified": False, "saved": False}
                    if not matched: response["error"] = "Wrong project"
                    peer.sendall(json.dumps(response).encode())
            return requests
        profiles = []
        for listener, project in zip(listeners, projects):
            with patch.dict(os.environ, {"UNREAL_MCP_PORT": str(listener.getsockname()[1]), "UNREAL_MCP_EXPECTED_PROJECT": project}):
                profiles.append(test_grouped_tools.GroupedServerTests().load_server())
        with ThreadPoolExecutor(max_workers=2) as workers:
            servers = [workers.submit(serve, listener, project) for listener, project in zip(listeners, projects)]
            clients = [profile.UnrealConnection() for profile in profiles]
            for client, project in zip(clients, projects):
                result = client.send_command("get_class_properties", {"asset_path": "/Game/Shared/DA_Test"})
                self.assertEqual(result["result"]["project"], project)
            self.assertEqual(clients[0].send_command("get_class_properties", {"asset_path": "/Game/Shared/DA_Test"})["result"]["project"], projects[0])
            with patch.object(profiles[0], "UNREAL_PORT", listeners[1].getsockname()[1]):
                refused = clients[0].send_command("close_editor", {})
                self.assertEqual(refused["result"]["execution_state"], "not_started")
            histories = [server.result(timeout=10) for server in servers]
            self.assertEqual([len(history) for history in histories], [2, 2])

    def test_bound_native_refusal_preserves_not_started_receipt(self):
        response = {"status": "error", "error": "Wrong project", "project_binding": {
            "contract": 1, "project_path": "C:/Projects/SkillTest/SkillTest.uproject", "matched": False},
            "result": {"success": False, "execution_state": "not_started", "modified": False, "saved": False, "stage": "project_binding"}}
        sock = Mock(spec=socket.socket); sock.recv.return_value = json.dumps(response).encode()
        def connect():
            self.connection.socket = sock
            return True
        with patch.object(self.server, "UNREAL_EXPECTED_PROJECT", "C:/Projects/NF/NF.uproject"), patch.object(self.connection, "connect", side_effect=connect) as connected:
            self.assertEqual(self.connection.send_command("close_editor", {}), response)
            connected.assert_called_once(); sock.sendall.assert_called_once()

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