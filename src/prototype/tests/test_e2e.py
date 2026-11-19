"""End-to-end tests: HTTP -> Python -> JSONL -> C++ ChatOrchestrator -> ToolDispatcher.
Run from the project root: REFLECTION_WORKER=./build/reflection_chat_worker python3 -m unittest discover -s prototype/tests -v
"""
import json
import time
import os
from pathlib import Path
import sys
import threading
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from server import Handler, WorkerBridge


class WorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        worker = Path(os.environ.get('REFLECTION_WORKER', 'build/reflection_chat_worker')).resolve()
        if not worker.exists():
            raise RuntimeError(f'Build worker first: {worker}')
        cls.bridge = WorkerBridge(worker, 'mock')
        Handler.bridge = cls.bridge
        Handler.secret = 'test-only-secret'
        cls.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.url = f'http://127.0.0.1:{cls.server.server_address[1]}'

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.bridge.close()
        cls.thread.join(timeout=3)

    def action(self, **payload):
        request = urllib.request.Request(
            self.url + '/api/action',
            data=json.dumps(payload).encode(),
            headers={
                'Content-Type':'application/json',
                'X-Local-Token':Handler.secret,
                'Origin':self.url,
            },
            method='POST',
        )
        with urllib.request.urlopen(request, timeout=10) as response:
            self.assertEqual(response.status, 200)
            return [json.loads(line) for line in response]

    def test_01_static_and_csrf(self):
        with urllib.request.urlopen(self.url + '/', timeout=5) as response:
            body = response.read().decode()
        self.assertIn(Handler.secret, body)
        self.assertIn('Reflection Chat', body)
        request = urllib.request.Request(self.url + '/api/action', data=b'{"action":"snapshot"}',
                                         method='POST', headers={'Content-Type':'application/json'})
        with self.assertRaises(urllib.error.HTTPError) as err:
            urllib.request.urlopen(request, timeout=5)
        self.assertEqual(err.exception.code, 403)

    def test_02_introspection(self):
        events = self.action(action='snapshot')
        self.assertEqual(events[-1]['type'], 'complete')
        s = events[-1]['snapshot']
        self.assertEqual(s['mode'], 'mock')
        self.assertEqual(len(s['documents']), 3)
        self.assertEqual(len(s['tools']), 3)
        tools = {t['name']:t for t in s['tools']}
        self.assertEqual(tools['documents_rename']['access'], 'write')
        self.assertTrue(tools['documents_rename']['requires_confirmation'])
        self.assertEqual(tools['documents_list']['access'], 'read_only')
        self.assertEqual({t['type'] for t in s['types']}, {'Document','DocumentService'})

    def test_03_list_and_search(self):
        for prompt, expected in [('Liste alle Dokumente', '3 Dokument(e)'),
                                  ('Suche nach Bericht', '2 Dokument(e)')]:
            events = self.action(action='send', text=prompt)
            self.assertTrue(any(e['type']=='model_request' for e in events))
            self.assertTrue(any(e['type']=='tool_requested' for e in events))
            self.assertTrue(any(e['type']=='tool' and e['code']=='ok' for e in events))
            self.assertTrue(any(e['type']=='delta' for e in events))
            self.assertEqual(events[-1]['type'], 'complete')
            self.assertIn(expected, events[-1]['answer'])

    def test_04_deny_then_approve(self):
        prompt = 'Benenne Dokument 1 um in Abschlussbericht'
        events = self.action(action='send', text=prompt)
        self.assertEqual(events[-1]['type'], 'approval')
        request = events[-1]
        self.assertEqual(request['tool_name'], 'documents_rename')
        self.assertEqual(request['arguments'], {'id':1, 'title':'Abschlussbericht'})
        self.assertEqual(request['snapshot']['documents'][0]['title'], 'Projektbericht 2026')
        events = self.action(action='resume', approval_id=request['approval_id'], approved=False)
        self.assertEqual(events[-1]['type'], 'complete')
        self.assertEqual(events[-1]['snapshot']['documents'][0]['title'], 'Projektbericht 2026')
        events = self.action(action='send', text=prompt)
        request = events[-1]
        self.assertEqual(request['type'], 'approval')
        events = self.action(action='resume', approval_id=request['approval_id'], approved=True)
        self.assertEqual(events[-1]['type'], 'complete')
        self.assertEqual(events[-1]['snapshot']['documents'][0]['title'], 'Abschlussbericht')
        self.assertTrue(any(e['type']=='tool' and e['code']=='ok' for e in events))

    def test_05_reset_retains_worker_state(self):
        events = self.action(action='reset')
        self.assertEqual(events[-1]['type'], 'complete')
        s = events[-1]['snapshot']
        self.assertEqual(s['documents'][0]['title'], 'Abschlussbericht')
        self.assertFalse(s['failed'])
        events = self.action(action='send', text='Liste alle Dokumente')
        self.assertEqual(events[-1]['type'], 'complete')
        self.assertIn('Abschlussbericht', events[-1]['answer'])

    def test_06_cancel_and_reset(self):
        result = {}
        def slow():
            result['events'] = self.action(action='send', text='Bitte langsam antworten')
        running = threading.Thread(target=slow, daemon=True)
        running.start()
        time.sleep(0.25)
        stop = self.action(action='cancel')
        self.assertEqual(stop[-1]['type'], 'complete')
        self.assertEqual(stop[-1]['status'], 'cancel_requested')
        running.join(timeout=8)
        self.assertFalse(running.is_alive())
        self.assertEqual(result['events'][-1]['type'], 'cancelled')
        reset = self.action(action='reset')
        self.assertEqual(reset[-1]['type'], 'complete')
        self.assertFalse(reset[-1]['snapshot']['failed'])

    def test_07_invalid_approval(self):
        events = self.action(action='resume', approval_id='not-a-real-approval', approved=True)
        self.assertEqual(events[-1]['type'], 'error')
        self.action(action='reset')


if __name__ == '__main__':
    unittest.main(verbosity=2)
