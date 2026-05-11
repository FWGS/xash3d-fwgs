# NAT bypass feature in Xash3D FWGS
Since IPv6 not as widespread as we would like, NAT (Network Address Translation) still being actively used by many internet service providers in an attempts to
mitigate IPv4 addresses exhaustion. In short, they uses one IPv4 address and doing some tricks with ports to represent many other users behind this address.

But this leads to a problem for users: they cannot accept direct
incoming connections anymore. This means that if you are behind provider's NAT and will try to setup Xash3D FWGS server - nobody will be able to connect to it,
and the server will not show up in servers public list.

## Is it possible to avoid this problem?
In most cases, it is possible to bypass NAT with UDP hole punching, and Xash3D FWGS uses this method too. But this method is not 100% guaranteed to work - it depends
on NAT configurations on both server and client side, and there is no way to control it.

First of all, server should not be behind symmetric NAT. You can check your NAT type on [this page](https://www.checkmynat.com/).
If you get "Symmetric NAT" result in this test, that means you cannot setup publicly available server with internet connection that you are using.

Here is more detailed scheme of different NAT types compatibility, it explains are users with different NAT types can connect to each other or not.
Client NAT type on rows, server NAT type on columns:

| *NAT Type*           | Full Cone | Restricted Cone | Port Restricted Cone | Symmetric |
| -------------------- | --------- | --------------- | -------------------- | --------- |
| Full Cone            | ✔️        | ✔️              | ✔️                    | ✔️        | 
| Restricted Cone      | ✔️        | ✔️              | ✔️                    | ✔️        |
| Port Restricted Cone | ✔️        | ✔️              | ✔️                    | ❌        |
| Symmetric            | ✔️        | ✔️              | ❌                   | ❌        |

## How to use NAT bypass feature?
If you are starting server within the game, you need to enable option "*Use NAT Bypass instead of direct mode*" in bottom left corner of screen.

If you are starting dedicated server, you should add console variable `sv_nat 1` to server.cfg file, or add `+sv_nat 1` to server startup parameters.

If you need to connect to server behind NAT, there is separate tab named *NAT* for such servers in server browser. Note that it is useless to add NAT server to favorites,
or somehow trying manually to store it, because it has changing address and port.
