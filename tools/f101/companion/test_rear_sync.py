import unittest
from rear_sync import RearSync

class RearTests(unittest.TestCase):
    def setUp(self):
        self.phone='moon';self.watch='settings';self.writes=[]
        self.sync=RearSync(lambda:self.phone,self.phone_set,lambda:{'face':self.watch},self.watch_set,
                           {f:f for f in ['moon','settings','pocketwatch']})
    def phone_set(self,f):self.phone=f
    def watch_set(self,f):self.writes.append(f);self.watch=f;return {'face':f}
    def test_initial_and_watch_changes_not_echoed(self):
        self.sync.poll();self.assertEqual(self.phone,'settings')
        self.watch='moon';self.sync.poll();self.sync.poll()
        self.assertEqual(self.phone,'moon');self.assertEqual(self.writes,[])
    def test_phone_swipe(self):
        self.sync.poll();self.phone='pocketwatch';self.sync.poll();self.sync.poll()
        self.assertEqual(self.watch,'pocketwatch');self.assertEqual(self.writes,['pocketwatch'])
    def test_swipe_during_write_is_not_lost(self):
        self.sync.poll();self.phone='moon'
        def write(f):
            self.phone='pocketwatch';return self.watch_set(f)
        self.sync.ww=write;self.sync.poll();self.sync.ww=self.watch_set;self.sync.poll()
        self.assertEqual(self.writes,['moon','pocketwatch'])
    def test_reconnect_discards_offline_swipes(self):
        self.sync.poll();self.phone='moon'
        def fail():raise OSError('offline')
        self.sync.rw=fail;self.sync.poll()
        self.sync.rw=lambda:{'face':'settings'};self.sync.poll()
        self.assertEqual(self.phone,'settings');self.assertEqual(self.writes,[])
    def test_unknown_face_is_not_written(self):
        self.sync.poll();self.phone='unsupported';r=self.sync.poll()
        self.assertFalse(r['synced']);self.assertEqual(self.writes,[])

if __name__=='__main__':unittest.main()
