using System.IO.Ports;
using System.Management;
using System.Text.RegularExpressions;
using SolderStation.Models;

namespace SolderStation.Services;

/// <summary>Lists serial ports with their Windows device names.</summary>
public static partial class PortScanner
{
    /// <summary>USB VID/PID of the station firmware (ST virtual COM port).</summary>
    private const string StationHardwareId = "VID_0483&PID_5740";

    [GeneratedRegex(@"\((COM\d+)\)", RegexOptions.IgnoreCase)]
    private static partial Regex ComNameRegex();

    public static IReadOnlyList<PortInfo> Scan()
    {
        var names = SerialPort.GetPortNames().Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        var details = new Dictionary<string, (string Desc, bool Station)>(StringComparer.OrdinalIgnoreCase);

        if (OperatingSystem.IsWindows())
        {
            try
            {
                using var searcher = new ManagementObjectSearcher(
                    "SELECT Name, PNPDeviceID FROM Win32_PnPEntity WHERE Name LIKE '%(COM%'");
                foreach (var mo in searcher.Get().Cast<ManagementObject>())
                {
                    var name = mo["Name"] as string ?? "";
                    var pnp = mo["PNPDeviceID"] as string ?? "";
                    var m = ComNameRegex().Match(name);
                    if (!m.Success) continue;
                    var com = m.Groups[1].Value;
                    var desc = name[..m.Index].Trim();
                    details[com] = (desc, pnp.Contains(StationHardwareId, StringComparison.OrdinalIgnoreCase));
                    if (!names.Contains(com, StringComparer.OrdinalIgnoreCase)) names.Add(com);
                }
            }
            catch (Exception)
            {
                // WMI is optional: plain port names are enough
            }
        }

        return names
            .Select(n => details.TryGetValue(n, out var d) ? new PortInfo(n, d.Desc, d.Station) : new PortInfo(n, "", false))
            .OrderByDescending(p => p.IsStation)
            .ThenBy(p => PortNumber(p.Name))
            .ToList();
    }

    private static int PortNumber(string name) =>
        int.TryParse(new string(name.Where(char.IsDigit).ToArray()), out int n) ? n : int.MaxValue;
}
