import pathlib, subprocess, tempfile, unittest, shutil

ROOT = pathlib.Path(__file__).resolve().parents[1]
HDR = ROOT / "engine/platform/gamecube"

C = r'''
#include <stdio.h>
#include <string.h>
#include "net_dns.h"
#define CHECK(x) do{ if(!(x)){ printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } }while(0)
int main(void){
	uint8_t q[512], r[512], ip[4];
	size_t n = GC_DnsBuildQuery(q, sizeof q, 0x1234, "ms.example.com");
	CHECK(n == 12 + 1+2 + 1+7 + 1+3 + 1 + 4);
	CHECK(q[0]==0x12 && q[1]==0x34 && q[12]==2 && q[13]=='m');
	CHECK(GC_DnsBuildQuery(q, sizeof q, 1, "")==0);
	CHECK(GC_DnsBuildQuery(q, sizeof q, 1, "a..b")==0);
	CHECK(GC_DnsBuildQuery(q, 20, 1, "ms.example.com")==0);
	/* response: echo question, CNAME (compressed) then A record */
	n = GC_DnsBuildQuery(q, sizeof q, 0x1234, "ms.example.com");
	memcpy(r, q, n);
	r[2]=0x81; r[3]=0x80; r[7]=2;
	size_t p = n;
	uint8_t cname[] = {0xC0,0x0C, 0,5, 0,1, 0,0,0,60, 0,2, 0xC0,0x0C};
	memcpy(r+p, cname, sizeof cname); p += sizeof cname;
	uint8_t a[] = {0xC0,0x0C, 0,1, 0,1, 0,0,0,60, 0,4, 203,0,113,7};
	memcpy(r+p, a, sizeof a); p += sizeof a;
	CHECK(GC_DnsParseResponse(r, p, 0x1234, ip));
	CHECK(ip[0]==203 && ip[1]==0 && ip[2]==113 && ip[3]==7);
	CHECK(!GC_DnsParseResponse(r, p, 0x9999, ip));      /* id mismatch */
	CHECK(!GC_DnsParseResponse(r, p-3, 0x1234, ip));    /* truncated */
	r[3]=0x83;                                          /* NXDOMAIN */
	CHECK(!GC_DnsParseResponse(r, p, 0x1234, ip));
	puts("ok");
	return 0;
}
'''

@unittest.skipUnless(shutil.which("gcc") or shutil.which("cc"), "no host C compiler")
class DnsTests(unittest.TestCase):
    def test_dns_roundtrip(self):
        cc = shutil.which("gcc") or shutil.which("cc")
        with tempfile.TemporaryDirectory() as d:
            src = pathlib.Path(d) / "t.c"
            src.write_text(C)
            exe = pathlib.Path(d) / "t"
            subprocess.run([cc, "-Wall", "-Werror", f"-I{HDR}", str(src), "-o", str(exe)], check=True)
            out = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stdout)

    def test_gethostbyname_uses_resolver(self):
        s = (HDR / "net_gamecube.c").read_text()
        self.assertIn("GC_DnsResolve( addrString", s)

if __name__ == "__main__":
    unittest.main()
