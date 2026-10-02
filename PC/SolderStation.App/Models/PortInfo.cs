namespace SolderStation.Models;

/// <summary>Serial port with a human readable description.</summary>
public sealed record PortInfo(string Name, string Description, bool IsStation)
{
    public string Display => IsStation
        ? $"{Name} — T12 Station (USB)"
        : string.IsNullOrEmpty(Description) ? Name : $"{Name} — {Description}";

    public override string ToString() => Display;
}
