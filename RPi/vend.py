from blockcypher import get_address_overview as details
import urllib2
import sys
import time


addr = '1BLAUugHT67PJnJARvV7AADjxFB6pDQ75i'
count = float('inf')
oldbal = float('inf')
price = 540
testflag = False


if len(sys.argv) >= 2:
    if bool(sys.argv[1]) is True:
        testflag = True
    else:
        import RPi.GPIO as GPIO
        GPIO.setmode(GPIO.BOARD)
        GPIO.setup(12, GPIO.OUT, initial=GPIO.LOW)
        if bool(sys.argv[1]) is False:
            addr = str(sys.argv[1])
            print addr
    if len(sys.argv) >= 3:
        count = int(sys.argv[2])
        print count


def pricefetch():
    page = urllib2.urlopen("https://blockchain.info/q/24hrprice")
    pageinfo = float(page.read())
    return pageinfo


if addr != "":
    while count != 0:
        price = pricefetch()
        print price
        satprice = price / 1e8
        print satprice
        info = details(addr)
        curbal = info["final_balance"]
        print curbal
        if curbal != oldbal:
            print "Change!"
            diff = curbal - oldbal
            if diff*satprice >= 0.95:
                print "Received %s satoshis!" % diff
                if testflag is True:
                    print "Valid transaction!"
                else:
                    print "Registering currency!"
                    GPIO.output(12, True)
                    time.sleep(0.1)
                    GPIO.output(12, False)
        oldbal = curbal
        count -= 1
        time.sleep(20)
