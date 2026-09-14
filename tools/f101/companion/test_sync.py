import unittest
from sync import Sync

class FaceSyncTests(unittest.TestCase):
    def test_watch_change_is_not_echoed(self):
        face=['pocketwatch']; writes=[]
        sync=Sync(lambda: {'face':face[0]},lambda f:writes.append(f))
        sync.step();face[0]='moon';sync.step()
        self.assertEqual(sync.snapshot()['face'],'moon');self.assertEqual(writes,[])
    def test_phone_change_waits_for_confirmation(self):
        writes=[]
        def select(f):writes.append(f);return {'face':f}
        sync=Sync(lambda:{'face':'pocketwatch'},select);sync.step();sync.request('moon')
        self.assertEqual(sync.snapshot()['face'],'pocketwatch')
        sync.step();self.assertEqual(sync.snapshot()['face'],'moon');self.assertEqual(writes,['moon'])
    def test_disconnect_drops_pending_write(self):
        def fail(f):raise OSError('offline')
        sync=Sync(lambda:{'face':'settings'},fail);sync.step();sync.request('moon');sync.step()
        self.assertIsNone(sync.snapshot()['pending']);self.assertFalse(sync.snapshot()['connected'])
        sync.step();self.assertEqual(sync.snapshot()['face'],'settings')
    def test_busy_and_disconnected_requests_rejected(self):
        sync=Sync(lambda:{'face':'moon'},lambda f:{'face':f})
        with self.assertRaises(ValueError):sync.request('sky')
        sync.step();sync.request('sky')
        with self.assertRaises(ValueError):sync.request('moon')
    def test_swipe_arriving_during_read_is_preserved(self):
        sync=Sync(lambda:{'face':'moon'},lambda f:{'face':f});sync.step()
        def read():
            sync.request('sky')
            return {'face':'moon'}
        sync.read=read;sync.step()
        self.assertEqual(sync.snapshot()['pending'],'sky')
        sync.step();self.assertEqual(sync.snapshot()['face'],'sky')
    def test_wrong_confirmation_not_success(self):
        sync=Sync(lambda:{'face':'moon'},lambda f:{'face':'moon'});sync.step();sync.request('sky');sync.step()
        self.assertFalse(sync.snapshot()['connected']);self.assertEqual(sync.snapshot()['face'],'moon')

if __name__=='__main__':unittest.main()
