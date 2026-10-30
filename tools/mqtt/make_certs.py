"""Test certificates for mutual TLS between the device and the test broker (needs `pip install cryptography`).

  python tools/mqtt/make_certs.py --host 172.30.1.84 [--device blob-4c08] [--out tools/mqtt/certs]

Writes, into the output folder (git ignores it; the keys are test keys, never for a real broker):
  ca.pem / ca.key             the CA that signs everything below
  server.pem / server.key     the broker's certificate; its name is --host (the device connects by
                              that name and checks it: a name only, no subjectAltName, which the
                              device's mbedTLS 2.28 matches against the common name)
  <device>.pem / <device>.key the device's certificate (RSA 2048, as the digital signature
                              peripheral needs) and key
  other-ca.pem, other-<device>.pem/.key   a second CA and a device certificate of it, to try that the
                              broker turns a certificate of the wrong CA away

Use with: blob_mqtt.py broker --tls-port 8883 --tls-cert server.pem --tls-key server.key --client-ca ca.pem
and docs/ARCHITECTURE 11.2a ("mutual TLS") for loading ca.pem, <device>.pem and <device>.key into the device.
"""
import argparse
import datetime
import os

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID


def new_key():
    return rsa.generate_private_key(public_exponent=65537, key_size=2048)


def name(common_name):
    return x509.Name([x509.NameAttribute(NameOID.ORGANIZATION_NAME, "blob test"),
                      x509.NameAttribute(NameOID.COMMON_NAME, common_name)])


def issue(subject, key, issuer_name, issuer_key, days, ca=False, usage=None):
    now = datetime.datetime.now(datetime.timezone.utc)
    b = (x509.CertificateBuilder().subject_name(subject).issuer_name(issuer_name).public_key(key.public_key())
         .serial_number(x509.random_serial_number()).not_valid_before(now - datetime.timedelta(days=1))
         .not_valid_after(now + datetime.timedelta(days=days))
         .add_extension(x509.BasicConstraints(ca=ca, path_length=None), critical=True))
    if usage:
        b = b.add_extension(x509.ExtendedKeyUsage([usage]), critical=False)
    return b.sign(issuer_key, hashes.SHA256())


def write(folder, base, cert=None, key=None):
    if cert is not None:
        with open(os.path.join(folder, base + ".pem"), "wb") as f:
            f.write(cert.public_bytes(serialization.Encoding.PEM))
    if key is not None:
        with open(os.path.join(folder, base + ".key"), "wb") as f:
            f.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.TraditionalOpenSSL,
                                      serialization.NoEncryption()))


def make_ca(folder, base, common_name):
    key = new_key()
    cert = issue(name(common_name), key, name(common_name), key, 3650, ca=True)
    write(folder, base, cert, key)
    return cert, key


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", required=True, help="the name or address the device connects to")
    p.add_argument("--device", default="blob-device", help="the device's certificate name")
    p.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "certs"))
    args = p.parse_args()
    os.makedirs(args.out, exist_ok=True)

    ca_cert, ca_key = make_ca(args.out, "ca", "blob test CA")
    server_key = new_key()
    write(args.out, "server", issue(name(args.host), server_key, ca_cert.subject, ca_key, 365,
                                    usage=ExtendedKeyUsageOID.SERVER_AUTH), server_key)
    device_key = new_key()
    write(args.out, args.device, issue(name(args.device), device_key, ca_cert.subject, ca_key, 365,
                                       usage=ExtendedKeyUsageOID.CLIENT_AUTH), device_key)

    other_cert, other_key = make_ca(args.out, "other-ca", "some other CA")
    stranger_key = new_key()
    write(args.out, "other-" + args.device, issue(name(args.device), stranger_key, other_cert.subject, other_key, 365,
                                                  usage=ExtendedKeyUsageOID.CLIENT_AUTH), stranger_key)
    for f in sorted(os.listdir(args.out)):
        print(f"{os.path.getsize(os.path.join(args.out, f)):5d} bytes  {f}")


if __name__ == "__main__":
    main()
