import os 


HOST = 'http://192.168.0.122/notify'



def notify(message, submessage):
    # GET request
    # encode to html
    message = message.replace(' ', '%20')
    
    os.system('curl -X GET "' + HOST + '?message=' + message + '&submessage=' + submessage + '"')
    return

def read_dbus():

    with open('dbus.log', 'r') as f:
        lines = f.readlines()
        for line in lines:
            if 'string' in line:
                message = line.split('string')[1].strip().replace('"', '')
                print(message)
                notify('Message', message)


def main():
    # dbus-monitor "interface='org.freedesktop.Notifications'

    os.system('dbus-monitor "interface=\'org.freedesktop.Notifications\'" > dbus.log')
    while True:
        read_dbus()

    return

if __name__ == '__main__':
    main()