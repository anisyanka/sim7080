# SIM7080 + NB-Iot + MQTT
SIM7080 AT-command based async low level independent driver to use in embedded systems.
\
\
The AT Command set implemented by SIM7080 Series is a combination of 3GPP TS 27.005,
3GPP TS 27.007 and ITU-T recommendation V.25ter and the AT commands developed by
SIMCom.
\
\
**Supported networks:**
 - NB-Iot

**Supported protocols:**
 - MQTT

## Setup SIM7080
```bash
AT+CREBOOT
AT
AT+CSCLK=0 # Disable entering sleep mode
AT+CPIN?   # Is SIM ready?
AT+CFUN=0  # Disable RF
AT+CNMP=2  # GSM or LTE is defined automatically
AT+CMNB=2  # NB-Iot

# Connect to NB-IOT
AT+CGDCONT=1,"IP","iot" # PDP context (шлюз до операторской сети)
AT+CNCFG=0,1,"iot"      # Set the APN manually. Some operators need to set APN first when registering the network.
AT+CFUN=1               # Full functionality
AT+CNACT=0,1            # Activate network bearer

# Get params
AT+CGDCONT?  # PDP context
AT+CEREG?    # Network Registration Status
AT+COPS?
AT+CCLK?     # Precize time
AT+CGPADDR=1 # Get local IP

# Upload device and server certificates 
AT+CFSTERM # Free the Flash Buffer Allocated by CFSINIT (if was)

# rootCA
AT+CFSINIT
AT+CFSWFILE=3,"rootCA.crt",0,1800,10000
<copy and send data from the YANDEX root cert file>
AT+CFSTERM

# Device cert
AT+CFSINIT
AT+CFSWFILE=3,"deviceCert.pem",0,1776,10000
<copy and send data from the DEVICE cert>
AT+CFSTERM

# Device private key
AT+CFSINIT
AT+CFSWFILE=3,"devicePrivateKey.pem",0,3220,10000
<copy and send data from the PRIVATE DEVICE KEY>
AT+CFSTERM

# Configure SSL parameters of a context identifier
AT+SMDISC
AT+CSSLCFG="SNI",0,"mqtt.cloud.yandex.net"

# Set MQTT Parameter
AT+SMCONF="URL","mqtt.cloud.yandex.net",8883
AT+SMCONF="CLIENTID",0
AT+SMCONF="KEEPTIME",60
AT+SMCONF="CLEANSS",1
AT+SMCONF="QOS",1
AT+SMCONF?

# Convert certs
AT+CSSLCFG="SSLVERSION",0,3
AT+CSSLCFG="CONVERT",2,"rootCA.crt"
AT+CSSLCFG="CONVERT",1,"deviceCert.pem","devicePrivateKey.pem"
AT+SMSSL=1,"rootCA.crt","deviceCert.pem"

# Connect to broker
AT+SMCONN

# Publish data
AT+SMPUB="$registries/are6phis3t903qjfrje3/events",93,0,1
{"ch1": "100", "ch1": "875759", "pressure": "20kPa", "charge": "76%", "safety_flags": "0x00"}
```
