import base64
import email
import imaplib
import json
import time
import re
import sys
from email.header import decode_header

import requests


def decode_utf7(s):
    i = 0
    builder = ''
    while i < len(s):
        c = s[i]
        if c == '&' and s[i+1] != '-':
            seq = ''
            while i < len(s):
                i += 1
                if s[i] == '-':
                    break
                seq += s[i]
                
            encoded = seq.replace(',', '/')
            pad = 4 - len(encoded) % 4
            encoded = encoded.ljust(len(encoded)+pad, '=')
            builder += base64.b64decode(encoded).decode('utf-16be')
        else:
            if c == '&' and s[i+1] == '-':
                i += 1
            builder += s[i]
        i += 1
    return builder

def encode_utf7(s):
    to_int32 = lambda c: int.from_bytes(c.encode('utf-32le'), 'little')
    i = 0
    builder = ''
    while i < len(s):
        c = s[i]
        cp = to_int32(c)
        if 0x1F < cp and cp < 0x7F:
            builder += c
            i += 1
            continue
        
        seq = c
        while i < len(s):
            i += 1
            cp = to_int32(s[i])
            if 0x1F < cp and cp < 0x7F:
                break
            seq += s[i]
        encoded = base64.b64encode(seq.encode('utf-16be')).decode('utf-8')
        builder += '&' + encoded.replace(',', '/').rstrip('=') + '-'
    return builder

def mine_decode(encoded):
    s, charset = decode_header(encoded)[0]
    try:
        if charset is None:
            s = s.decode('utf-8')
        else:
            s = s.decode(charset.split('*')[0])
    except:
        s = str(s)
    return s

if len(sys.argv) < 2:
    config_file = 'config.json'
else:
    config_file = sys.argv[1]

try:
    with open(config_file, encoding='utf-8') as f:
        config = json.load(f)
except:
    with open(config_file, 'w', encoding='utf-8') as f:
        default_config = {
            'IMAPServer': 'imap.gmail.com',
            'IMAPPort': 993,
            'IMAPSearch': '(UNSEEN)',
            'Email': '',
            'Password': '',
            'OSDServer': 'localhost',
            'OSDPort': 8520,
        }
        json.dump(default_config, f, indent=4, ensure_ascii=False)
    exit(1)

M = imaplib.IMAP4_SSL(config['IMAPServer'], config['IMAPPort'])
resp, msg = M.login(config['Email'], config['Password'])
if resp != 'OK':
    raise Exception(f"Unable to login to sever {config['IMAPServer']}: {msg.decode('utf-8')}")

if 'Mailbox' not in config:
    config['Mailbox'] = 'INBOX'

config['MailboxList'] = []
for i in M.list()[1]:
    config['MailboxList'] += [decode_utf7(i.decode('utf-8').split(' "/" ')[-1])]

print('Avaliable mailboxes:')
print('\n\t'.join(config['MailboxList']))

M.logout()

HEAD = -1
if 'HEAD' in config:
    HEAD = config['HEAD']

while True:
    try:
        print('Checking for new email')

        M = imaplib.IMAP4_SSL(config['IMAPServer'], config['IMAPPort'])
        resp, msg = M.login(config['Email'], config['Password'])
        if resp != 'OK':
            raise Exception(f"Unable to login to sever {config['IMAPServer']}: {msg.decode('utf-8')}")

        M.select(encode_utf7(config['Mailbox']), readonly = True)
        typ, data = M.search(None, config['IMAPSearch'])

        mail_list = data[0].split()
        if HEAD < 0:
            HEAD = int(data[0].split()[-1]) - 1

        for num in mail_list:
            if int(num) <= HEAD:
                continue

            typ, data = M.fetch(num, 'BODY.PEEK[]')
            
            msg = email.message_from_bytes(data[0][1])
            print(f'New email({num}): ', msg.get('Date'))

            sender = msg.get('From')
            m = re.match('"?(.*)"? <(.*@.*)>', sender) 
            if m is not None:
                nickname = mine_decode(m[1])
                address = m[2]
                sender = f'{nickname} <{address}>'
            
            subject = mine_decode(msg.get('Subject'))
            
            print(sender)
            print(subject, '\n')

            text = sender + '\n\n' + subject
            url = f"http://{config['OSDServer']}:{config['OSDPort']}/toast?title=新邮件&text={text}"
            r = requests.get(url)
            if not r.ok:
                print("unable to send notification")

        M.logout()
        if HEAD == int(mail_list[-1]):
            print('No new email found')

        HEAD = int(mail_list[-1])
        
        time.sleep(10)
    except KeyboardInterrupt:
        config['HEAD'] = HEAD
        with open('config.json', 'w', encoding='utf-8') as f:
            json.dump(config, f, indent=4, ensure_ascii=False)
        exit(0)