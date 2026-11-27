namespace Twist.App.Services;

// Minimal PCM WAV reader (8/16/24/32-bit integer, 32-bit float), the
// channels mixed to mono - for replaying a recording through the
// receiver.
public static class Wav
{
    public static (float[] samples, int fs) ReadMono(string path)
    {
        using var r = new BinaryReader(File.OpenRead(path));
        if (new string(r.ReadChars(4)) != "RIFF") throw new InvalidDataException("not a RIFF file");
        r.ReadUInt32();
        if (new string(r.ReadChars(4)) != "WAVE") throw new InvalidDataException("not a WAVE file");
        int fmt = 0, ch = 0, fs = 0, bits = 0;
        while (r.BaseStream.Position + 8 <= r.BaseStream.Length)
        {
            string id = new string(r.ReadChars(4));
            uint size = r.ReadUInt32();
            long next = r.BaseStream.Position + size + (size & 1);
            if (id == "fmt ")
            {
                fmt = r.ReadUInt16(); ch = r.ReadUInt16(); fs = (int)r.ReadUInt32();
                r.ReadUInt32(); r.ReadUInt16(); bits = r.ReadUInt16();
                // WAVE_FORMAT_EXTENSIBLE (many recorders): the real format is
                // the first two bytes of the sub-format GUID
                if (fmt == 0xFFFE && size >= 40) { r.ReadBytes(8); fmt = r.ReadUInt16(); }
            }
            else if (id == "data")
            {
                if (ch == 0) throw new InvalidDataException("data before fmt");
                if (!(fmt == 1 && bits is 8 or 16 or 24 or 32) && !(fmt == 3 && bits is 32 or 64))
                    throw new InvalidDataException(Lang.L.F("format non pris en charge (code {0}, {1} bits) : PCM 8 à 32 bits ou flottant attendu", fmt, bits));
                int bps = bits / 8;
                long avail = Math.Min(size, r.BaseStream.Length - r.BaseStream.Position);
                int n = (int)(avail / (bps * ch));
                var x = new float[n];
                byte[] raw = r.ReadBytes(n * bps * ch);
                bool isFloat = fmt == 3 && bits == 32;
                for (int i = 0; i < n; i++)
                {
                    double acc = 0;
                    for (int c = 0; c < ch; c++)
                    {
                        int o = (i * ch + c) * bps;
                        acc += fmt == 3 && bits == 64 ? BitConverter.ToDouble(raw, o) : isFloat ? BitConverter.ToSingle(raw, o) : bits switch
                        {
                            8 => (raw[o] - 128) / 128.0,
                            16 => BitConverter.ToInt16(raw, o) / 32768.0,
                            24 => ((raw[o] | raw[o + 1] << 8 | raw[o + 2] << 16) << 8 >> 8) / 8388608.0,
                            _ => BitConverter.ToInt32(raw, o) / 2147483648.0,
                        };
                    }
                    x[i] = (float)(acc / ch);
                }
                return (x, fs);
            }
            r.BaseStream.Position = next;
        }
        throw new InvalidDataException("no data chunk");
    }

    // 16-bit PCM mono, with an optional comment (LIST/INFO/ICMT chunk,
    // after the data: the decoders, which stop at the data, ignore it;
    // audio editors show it). Written through a temporary file.
    public static void WriteMono16(string path, ReadOnlySpan<float> x, int fs, string? comment = null)
    {
        byte[] info = [];
        if (!string.IsNullOrEmpty(comment))
        {
            var c = System.Text.Encoding.UTF8.GetBytes(comment + "\0");
            int cl = c.Length + (c.Length & 1);
            using var ms = new MemoryStream();
            using var lw = new BinaryWriter(ms);
            lw.Write("LIST"u8); lw.Write(4 + 8 + cl); lw.Write("INFO"u8);
            lw.Write("ICMT"u8); lw.Write(c.Length); lw.Write(c);
            if ((c.Length & 1) != 0) lw.Write((byte)0);
            lw.Flush();
            info = ms.ToArray();
        }
        var tmp = path + ".tmp";
        using (var w = new BinaryWriter(File.Create(tmp)))
        {
            int data = x.Length * 2;
            w.Write("RIFF"u8); w.Write(4 + 8 + 16 + 8 + data + info.Length); w.Write("WAVE"u8);
            w.Write("fmt "u8); w.Write(16); w.Write((short)1); w.Write((short)1); w.Write(fs); w.Write(fs * 2); w.Write((short)2); w.Write((short)16);
            w.Write("data"u8); w.Write(data);
            var buf = new byte[data];
            for (int i = 0; i < x.Length; i++)
            {
                short v = (short)Math.Clamp(Math.Round(x[i] * 32767.0), -32768, 32767);
                buf[2 * i] = (byte)v; buf[2 * i + 1] = (byte)(v >> 8);
            }
            w.Write(buf);
            w.Write(info);
        }
        File.Move(tmp, path, overwrite: true);
    }
}
