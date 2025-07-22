// --- IP address conversion utilities ---
String IPToString(uint32_t ip) {
    return String((ip >> 24) & 0xFF) + "." +
           String((ip >> 16) & 0xFF) + "." +
           String((ip >> 8) & 0xFF) + "." +
           String(ip & 0xFF);
}

bool StringToIP(const String& str, uint32_t& ip) {
    int parts[4] = {0};
    int lastPos = 0, pos = 0;
    for (int i = 0; i < 4; ++i) {
        pos = str.indexOf('.', lastPos);
        if (pos == -1 && i < 3) return false;
        String part = (i < 3) ? str.substring(lastPos, pos) : str.substring(lastPos);
        parts[i] = part.toInt();
        if (parts[i] < 0 || parts[i] > 255) return false;
        lastPos = pos + 1;
    }
    ip = ((uint32_t)parts[0] << 24) | ((uint32_t)parts[1] << 16) | ((uint32_t)parts[2] << 8) | (uint32_t)parts[3];
    return true;
} 