#include <pgmspace.h>

#define SECRET
#define THINGNAME "SENSOR_UpstairsHall"

const char WIFI_SSID[] = "CoronaRadiata_Guest";
const char WIFI_PASSWORD[] = "snakesquirrel";
const char AWS_IOT_ENDPOINT[] = "ayrai9uecmdnm-ats.iot.us-east-2.amazonaws.com";

// Amazon Root CA 1
static const char AWS_CERT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)EOF";

// Device Certificate
static const char AWS_CERT_CRT[] PROGMEM = R"KEY(
-----BEGIN CERTIFICATE-----
MIIDWjCCAkKgAwIBAgIVAIQuw7R757PonGdkKcUXxC2j8H4TMA0GCSqGSIb3DQEB
CwUAME0xSzBJBgNVBAsMQkFtYXpvbiBXZWIgU2VydmljZXMgTz1BbWF6b24uY29t
IEluYy4gTD1TZWF0dGxlIFNUPVdhc2hpbmd0b24gQz1VUzAeFw0yNDAxMjkwMjQy
NDdaFw00OTEyMzEyMzU5NTlaMB4xHDAaBgNVBAMME0FXUyBJb1QgQ2VydGlmaWNh
dGUwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDWIJxoOB/ZtTpIlUNm
+YawRti5c9fMPNY/Ucb/b8ZoKiEi83bnbDJxvURL63wyA0E3PGzzk4E50MAdgnEB
ahWmg2Do0YWrrYe1mmX2oulnWsug563z0inE78n3zAJ8XMGsCsldGQUwxkROvpRB
bRKysaJEBBaFiIYTvMDFpfHCo0wQsTbAZLWehi0Ep32l4Wezl6tIdvL7GMU80BeL
AvApJOV1PO6g0FgPuP9nftsIoDRVqKME/djP297wMz1qaGQ4BwNF/H5418p2Q0Mr
2JowLz2VhIT+cey9KQyNWkT+nuZ1MC3rTlWGJuOCqUZ25i5dfLr14i2O9cXU9qWF
liBXAgMBAAGjYDBeMB8GA1UdIwQYMBaAFB0CK0cAcSCiVTs8EhjfQyRzk3JWMB0G
A1UdDgQWBBTRkEKxQA9dJW0hFF/Wr6QbX1+/gjAMBgNVHRMBAf8EAjAAMA4GA1Ud
DwEB/wQEAwIHgDANBgkqhkiG9w0BAQsFAAOCAQEAibsJ7XAquNvXB4OeqdtUrOP3
UGakyw5LgbN8r55lq+Rmg8baHOUP5RT0o9jANzAbSr+LMWrTTvO+/wEYWFsf7d0+
d9qHz8gwjYjFn2nraICYNxaLSgHtraoAyz2PlOFFbixE4Pznm51/2eY5dDhzlIYr
r2sHJ/6PUGikxwSuWcSInnsKkPIihNDppgtPaJz4VMBBLvy4kE71hicVzEO7pYlB
GWkj4CzjG7wyh0IjqW/fQCGCaaT2sWiAdGMZgC2UfJFtleCg2/PY48gK9LCgR+Cv
b2CN8j6iNRWXsJxtXdtNMr0krSIo7ZJ2FUuKTPRp9/o+aL63nhR3TDeXan7WFA==
-----END CERTIFICATE-----
)KEY";

// Device Private Key
static const char AWS_CERT_PRIVATE[] PROGMEM = R"KEY(
-----BEGIN RSA PRIVATE KEY-----
MIIEpAIBAAKCAQEA1iCcaDgf2bU6SJVDZvmGsEbYuXPXzDzWP1HG/2/GaCohIvN2
52wycb1ES+t8MgNBNzxs85OBOdDAHYJxAWoVpoNg6NGFq62HtZpl9qLpZ1rLoOet
89IpxO/J98wCfFzBrArJXRkFMMZETr6UQW0SsrGiRAQWhYiGE7zAxaXxwqNMELE2
wGS1noYtBKd9peFns5erSHby+xjFPNAXiwLwKSTldTzuoNBYD7j/Z37bCKA0Vaij
BP3Yz9ve8DM9amhkOAcDRfx+eNfKdkNDK9iaMC89lYSE/nHsvSkMjVpE/p7mdTAt
605VhibjgqlGduYuXXy69eItjvXF1PalhZYgVwIDAQABAoIBABSoss5vU8taa9Qi
6ozd2Dvbqf9CiPiAih8z0aJFc3wLTIUlvjwXuUo1+4fjoWjl3X3py404lwgv9ski
E6sSRCJ7nHb5+A6XQHep/Npt310Eao8Mq0DR7wu8It1QhpDfFiR2KfVce564ElES
nBUv49TvxHsRdebORDlVRbgMCaZAk8gqMA15Q01rKBPgrP1OjtjnxZ8q6YLHb8dS
BRxrlMNuvZdemVENA3q7X9UExsezb7khwNlN+SZaiOvwT1kSOdwDr+FpEOtoy4s7
ehwf/9+sJ5iOm06yBFOgp1plTZTdkR/lE3I1o/TmTCHquVyIEc88ldUT30mx7TQN
n/fSdFECgYEA+il/6BpB1qXqA1HCTc6ZSYaTBsEA5H+PvrnWSsulwa0S+vbpTBKv
Luws2V3hzOenWkiiJAwZ0wZVJOX4zG850n7XUSp6mfjascUzN1ejcZbTt7X+/JSx
gmrRZNXzB1XiblLIGDYOupGjvnALlmpUKJU3xziaQ3mzGu4wSuzmuJ8CgYEA2x/V
1omUfKQN6V0INq8YqSY7Ucaj0e0k6zFSSYLOx+4kauoDxEEwzsgsiSJTlEikA5uu
wt3knIxINBbkMTLUFVZVzGEwGGKbSA96T57XWZoLbbbIi2Wb2sTM3blmrMoXcbf/
kuHxVkRiEMeuHTuEm4gfLMJF1bHv1cczqxETpUkCgYEAx0DWyCBh4H42bn5orWCp
Z47w/Kgt9dJExD1xGhIq7KDWRV71Y8peDpm+/0Nv3q2E9rxzqRKaXyLkHoeuK+dL
vwWYquWS0aENUBsqOCJt5MOlzuX/O1+UI7TBI/floyodOJJnHiGwiZoHOmA0WpJG
hiyUSPIkrSn6JrEhdgxLmTECgYAdS8JRLo+1olFh09hXR02qv7vKkR4x4NzAvRVd
UqnGvSYNTUA3queVndmmc/psptGW0eepkfUQoQi+PhlAIqWMPMC31H6TpAtStQ8L
OhdcmWXRw1BIossVkp45PqJlk5dS0uDOiHq1p+rch3XV3rE6Ahb46vhfO6zvDfTw
eBYCUQKBgQCpaxyt5ThpOcQTx6+/isYBy8DGfxF8Nczj+DXnypX41R1TJD903A0x
vnW7cX5gW5pFbYYMI7549kXLhHwK3Vjb78ubjqMCpuPlPSMz4UT5p1TnnoCYALr+
6u2wwu/BO2rcOJPGZWVhBkDlrtOOwiUV0bWUWNZPoHZ2hoB9eGo0nQ==
-----END RSA PRIVATE KEY-----
)KEY";