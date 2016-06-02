#!/usr/local/bin/python

import time
import blockcypher
import RPi.GPIO as GPIO
from datetime import datetime



#machine address
address = '1BLAUugHT67PJnJARvV7AADjxFB6pDQ75i'

#BlockCypher testnet address
testaddress = 'BwgSb6rdywfZyNDYRKu97bRAEeNWDkqZuo'

conf= set()
unconf = set()
utxonum = 0
conflist = []
unconflist = []
threshold = 165000


def checkwallet(addr, conf, unconf, utxonum):
    details = blockcypher.get_address_details(addr)
    # print details
    txlist = details['txrefs']
    print 'we have %s known txes' % len(txlist)
    for tx in txlist:
	if tx['tx_hash'] not in conf:
	    conflist.append({tx['tx_hash']: tx['value']})
    newtxlist = details['unconfirmed_txrefs']
    for utx in newtxlist:
	if utx['tx_hash'] in conf:
	    pass
	else:
	    unconflist.append({utx['tx_hash']:utx['value']})
    for utxo in unconf:
	if utxo in conf:
	    unconf.remove(utxo)
	    for el in unconflist:
		if utxo in el:
		    unconflist.remove(el)
    if len(unconf) > utxonum:
	utxonum = len(unconf)
	print "Genuinely new unconf!"
	print unconf
	return unconf
    else:
	return 0

GPIO.setmode(GPIO.BOARD)
GPIO.setup(12, GPIO.OUT, initial=GPIO.LOW)
scriptime = datetime.now()
home = '/home/pi/Desktop/vendtime'
oldstate=checkwallet(address, conf, unconf, utxonum)
fil = open('/home/pi/Desktop/vendtime' + str(scriptime), 'w')
fil.write(str(oldstate) + '\n')
fil.close()
#run check every 20 seconds
# time.sleep(20)
while True:
    state=checkwallet(address, conf, unconf, utxonum)
    cur = datetime.now()
    if state != 0:
        print 'new state is ' + str(state)
        fil=open(str(home) + str(scriptime), 'a')
	fil.write(str(cur) +"\n")
	fil.write('New state is ' + str(state) + "\n")
        fil.close()
	for tx in state:
	    for txh in unconflist:
		try:
		    if txh[tx] >= threshold:
			print "Sending signal to Arduino now"
		        fil=open(str(home) + str(scriptime), 'a')
	     	        fil.write(str(cur) +"\n")
		        fil.write("Sending signal to Arduino now\n")
		        fil.close()
			GPIO.output(12, True)
			time.sleep(0.1)
			GPIO.output(12, False)
		except:
		    pass
        time.sleep(20)
    if state == 0:
        fil=open(str(home) + str(scriptime), 'a')
	fil.write(str(cur) +"\n")
        fil.write('state is still ' + str(state) + '\n')
        fil.close()
        time.sleep(20)

