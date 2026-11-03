package com.steamwifi.controller;

import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InterfaceAddress;
import java.net.NetworkInterface;
import java.net.SocketException;
import java.net.UnknownHostException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

final class Networks {
    private Networks() {}

    /** Rede (Wi-Fi, Ethernet...) cuja sub-rede contém o endereço, ou null. */
    static Network findNetworkFor(ConnectivityManager cm, InetAddress target) {
        if (!(target instanceof Inet4Address)) return null;
        for (Network n : cm.getAllNetworks()) {
            LinkProperties lp = cm.getLinkProperties(n);
            if (lp == null) continue;
            for (LinkAddress la : lp.getLinkAddresses()) {
                InetAddress a = la.getAddress();
                if (a instanceof Inet4Address && sameSubnet(a, target, la.getPrefixLength())) return n;
            }
        }
        return null;
    }

    /** Primeira rede Wi-Fi ou Ethernet conectada, ou null. */
    static Network findLocalNetwork(ConnectivityManager cm) {
        for (Network n : cm.getAllNetworks()) {
            NetworkCapabilities caps = cm.getNetworkCapabilities(n);
            if (caps != null && (caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
                    || caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET))) {
                return n;
            }
        }
        return null;
    }

    /** Endereços de broadcast de todas as interfaces ativas (inclui o roteador do celular) + 255.255.255.255. */
    static List<InetAddress> broadcastAddresses() {
        List<InetAddress> out = new ArrayList<>();
        try {
            for (NetworkInterface ni : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!ni.isUp() || ni.isLoopback()) continue;
                for (InterfaceAddress ia : ni.getInterfaceAddresses()) {
                    InetAddress b = ia.getBroadcast();
                    if (b != null && !out.contains(b)) out.add(b);
                }
            }
        } catch (SocketException ignored) {
            // segue só com o broadcast global
        }
        try {
            InetAddress all = InetAddress.getByName("255.255.255.255");
            if (!out.contains(all)) out.add(all);
        } catch (UnknownHostException ignored) {
            // impossível para um IP literal
        }
        return out;
    }

    private static boolean sameSubnet(InetAddress a, InetAddress b, int prefix) {
        byte[] x = a.getAddress();
        byte[] y = b.getAddress();
        if (x.length != y.length) return false;
        int bits = Math.max(0, Math.min(prefix, x.length * 8));
        int i = 0;
        for (; bits >= 8; bits -= 8, i++) {
            if (x[i] != y[i]) return false;
        }
        if (bits == 0) return true;
        int mask = (0xFF << (8 - bits)) & 0xFF;
        return (x[i] & mask) == (y[i] & mask);
    }
}
