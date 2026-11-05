"""Reuse existing business regressions against the integrated Drogon entry.

Custom parser byte-level policies are specific to the legacy transport and are
not asserted here. Authentication, transitions and data safety still must pass.
"""
import os
import sys
import unittest
from pathlib import Path
import shutil

if os.name == 'nt':
    os.environ.setdefault('LMS_TEST_TEMP_ROOT', r'C:\Users\23974\Desktop\jk\x')
from stage1_http import Stage1

class IntegratedStage1(Stage1):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        target=Path(cls.temp.name)/'static'
        target.mkdir()
        shutil.copy2(Path(__file__).resolve().parents[1]/'static/index.html',target/'index.html')

    def test_existing_frontend_served(self):
        import http.client
        connection=http.client.HTTPConnection('127.0.0.1',self.port,timeout=3)
        try:
            connection.request('GET','/')
            response=connection.getresponse()
            self.assertEqual(response.status,200)
            self.assertIn('renderRoleMenu',response.read().decode('utf-8'))
        finally:
            connection.close()

transport_cases = {
    'test_fragmented_request_and_persistent_connection',
    'test_http_pipelining_keeps_next_request_bytes',
    'test_malformed_http_is_rejected',
    'test_truncated_body_does_not_invoke_login',
}
suite = unittest.TestSuite(IntegratedStage1(name) for name in
    unittest.defaultTestLoader.getTestCaseNames(IntegratedStage1) if name not in transport_cases)
result = unittest.TextTestRunner(verbosity=2).run(suite)
raise SystemExit(0 if result.wasSuccessful() else 1)
