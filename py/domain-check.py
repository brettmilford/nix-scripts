"""Check if a domain is vacant via WHOIS and DNS (NXDOMAIN)."""

import argparse
import socket
import whois
import whois.exceptions


def check_dns(domain: str) -> bool:
    """Return True if domain has no DNS records (NXDOMAIN)."""
    try:
        socket.getaddrinfo(domain, None)
        return False
    except socket.gaierror:
        return True


def check_whois(domain: str) -> bool:
    """Return True if WHOIS suggests domain is unregistered."""
    try:
        w = whois.whois(domain)
        return w.domain_name is None
    except whois.exceptions.WhoisError:
        return True
    except Exception:
        return False


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Check if domains are vacant via WHOIS and DNS"
    )
    parser.add_argument(
        "domains", nargs="+", help="One or more domains to check"
    )
    args = parser.parse_args()

    for domain in args.domains:
        dns_vacant = check_dns(domain)
        whois_vacant = check_whois(domain)
        status = "VACANT" if (dns_vacant and whois_vacant) else "TAKEN"
        print(
            f"{domain:30s}"
            f"  dns={'nxdomain' if dns_vacant else 'exists':10s}"
            f"  whois={'free' if whois_vacant else 'registered':12s}"
            f"  -> {status}"
        )


if __name__ == "__main__":
    main()
