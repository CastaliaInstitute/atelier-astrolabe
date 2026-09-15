"""Synchronize the installed F101 rear-display app and Astrolabe over BLE."""
import argparse
import fcntl
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent))
from server import astrolabe_ble_status, astrolabe_ble_select_face, ble_address
STATE=HERE.parent/'.state'
PACKAGE='castalia.institute.mynahastrolabe'
PREFS=Path('/proc/1/root/data/user/0')/PACKAGE/'shared_prefs/face-state.xml'
CATALOG=Path('/proc/1/root/data/media/0/Android/data')/PACKAGE/'files/rear-catalog'
ALIASES={'ota-face':'wifi-setup'}

def phone_face():
    for node in ET.fromstring(PREFS.read_text()):
        if node.get('name')=='selected_slug':return node.text
    raise RuntimeError('F101 has no selected face')

def select_phone(face):
    subprocess.run(['/usr/sbin/chroot','/proc/1/root','/system/bin/am','start-foreground-service',
                    '-n',PACKAGE+'/.RearDisplayService','-a',PACKAGE+'.SELECT_FACE','--es','slug',face],
                   check=True,capture_output=True,timeout=10)
    deadline=time.monotonic()+3
    while time.monotonic()<deadline:
        if phone_face()==face:return
        time.sleep(.1)
    raise RuntimeError('Rear-display app did not confirm '+face)

class RearSync:
    def __init__(self, read_phone, write_phone, read_watch, write_watch, mapping):
        self.rp,self.wp,self.rw,self.ww=read_phone,write_phone,read_watch,write_watch
        self.mapping=mapping
        self.reverse={v:k for k,v in mapping.items()}
        self.previous=None
        self.connected=False

    def step(self):
        # Watch is authoritative on first connection and after any failed exchange.
        observed=self.rw()['face']
        phone=self.rp()
        if self.connected and phone!=self.previous:
            self.previous=phone
            if phone not in self.mapping:
                return {'synced':False,'phone':phone,'watch':observed,'error':'Face unavailable on Astrolabe'}
            target=self.mapping[phone]
            confirmed=self.ww(target)['face']
            if confirmed!=target:raise RuntimeError('Watch did not confirm face')
            observed=confirmed
            # A new swipe during the BLE write is processed next time.
        else:
            if observed not in self.reverse:
                self.previous=phone;self.connected=True
                return {'synced':False,'phone':phone,'watch':observed,'error':'Face unavailable on F101'}
            target=self.reverse[observed]
            if phone!=target:self.wp(target)
            self.previous=target
        self.connected=True
        current=self.rp()
        return {'synced':self.mapping.get(current)==observed,'phone':current,'watch':observed}

    def poll(self):
        try:return self.step()
        except Exception as error:
            self.connected=False
            return {'synced':False,'error':str(error)}

def main(address):
    # Use the installed app's captured face catalog and this firmware's real slugs.
    source=(HERE.parents[2]/'astrolabe185b/main/faculty175_faces.c').read_text()
    watch=set(re.findall(r'\{ FACULTY175_FACE_\w+, "([a-z0-9-]+)"',source))
    phone=[p.stem.split('-',1)[1] for p in CATALOG.glob('*.ppm')]
    mapping={f:ALIASES.get(f,f) for f in phone if ALIASES.get(f,f) in watch}
    if not mapping:raise RuntimeError('No matching face catalog')
    sync=RearSync(phone_face,select_phone,lambda:astrolabe_ble_status(address),
                  lambda f:astrolabe_ble_select_face(address,f),mapping)
    with (STATE/'rear-sync.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        previous=None
        while True:
            status=sync.poll()
            if status!=previous:print(json.dumps(status),flush=True);previous=status
            status['updated']=time.time()
            temp=STATE/'rear-sync-status.tmp';temp.write_text(json.dumps(status));temp.replace(STATE/'rear-sync-status.json')
            time.sleep(1)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--address',required=True,type=ble_address)
    main(p.parse_args().address)
