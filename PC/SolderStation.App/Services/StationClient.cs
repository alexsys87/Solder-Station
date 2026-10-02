using System.IO;
using System.IO.Ports;
using System.Text;
using SolderStation.Models;

namespace SolderStation.Services;

/// <summary>Answer to a command: data lines ("= ...") and the final OK / ERR.</summary>
public sealed record Reply(bool Ok, int ErrorCode, string ErrorText, IReadOnlyList<string> Data);

/// <summary>The station answered "ERR code text".</summary>
public sealed class StationException : Exception
{
    public int Code { get; }

    public StationException(int code, string message) : base(message)
    {
        Code = code;
    }
}

/// <summary>
/// Serial transport for the station text protocol (USB CDC or UART).
/// One command is in flight at a time; a background thread reads lines and
/// routes them: "= ..." data and "OK"/"ERR" complete the pending command,
/// "!S ..." status and "!C" change notifications are raised as events.
/// </summary>
public sealed partial class StationClient : IDisposable
{
    private readonly SemaphoreSlim _commandLock = new(1, 1);
    private readonly object _sync = new();
    private readonly List<string> _data = new();
    private SerialPort? _port;
    private Thread? _reader;
    private volatile bool _running;
    private TaskCompletionSource<Reply>? _pending;

    /// <summary>Status stream line ("!S"). Raised on the reader thread.</summary>
    public event Action<StationStatus>? StatusReceived;

    /// <summary>Settings were changed on the device ("!C"). Raised on the reader thread.</summary>
    public event Action? DeviceSettingsChanged;

    /// <summary>Every line sent (true) or received (false). Raised on any thread.</summary>
    public event Action<string, bool>? LineLogged;

    /// <summary>The port was closed unexpectedly (cable unplugged).</summary>
    public event Action<string>? ConnectionLost;

    public bool IsOpen => _port?.IsOpen == true;
    public string? PortName => _port?.PortName;

    public void Open(string portName, int baudRate)
    {
        Close();
        var port = new SerialPort(portName, baudRate, Parity.None, 8, StopBits.One)
        {
            NewLine = "\n",
            Encoding = Encoding.ASCII,
            ReadTimeout = 250,
            WriteTimeout = 1000,
            Handshake = Handshake.None,
            DtrEnable = true,
            RtsEnable = true,
        };
        port.Open();
        port.DiscardInBuffer();
        _port = port;
        _running = true;
        _reader = new Thread(() => ReadLoop(port)) { IsBackground = true, Name = "StationReader" };
        _reader.Start();
    }

    public void Close()
    {
        _running = false;
        var port = _port;
        _port = null;
        if (port != null)
        {
            try { port.Close(); } catch { /* already gone */ }
            port.Dispose();
        }
        FailPending(Loc.T("X.PortClosed"));
    }

    public void Dispose() => Close();

    private void ReadLoop(SerialPort port)
    {
        while (_running)
        {
            string line;
            try
            {
                line = port.ReadLine();
            }
            catch (TimeoutException)
            {
                continue;
            }
            catch (Exception ex) when (ex is IOException or InvalidOperationException
                                           or UnauthorizedAccessException or OperationCanceledException)
            {
                if (_running && ReferenceEquals(port, _port))
                {
                    _running = false;
                    FailPending(Loc.T("X.Lost"));
                    ConnectionLost?.Invoke(ex.Message);
                }
                return;
            }

            line = line.Trim('\r', '\n', '\0', ' ');
            if (line.Length != 0) HandleLine(line);
        }
    }

    private void HandleLine(string line)
    {
        LineLogged?.Invoke(line, false);

        if (line.StartsWith("!S", StringComparison.Ordinal))
        {
            StatusReceived?.Invoke(StationStatus.Parse(line[2..]));
            return;
        }
        if (line == "!C")
        {
            DeviceSettingsChanged?.Invoke();
            return;
        }

        lock (_sync)
        {
            if (_pending == null) return;
            if (line.StartsWith('='))
            {
                _data.Add(line[1..].Trim());
            }
            else if (line == "OK")
            {
                _pending.TrySetResult(new Reply(true, 0, "", _data.ToArray()));
            }
            else if (line.StartsWith("ERR", StringComparison.Ordinal))
            {
                var parts = line.Split(' ', 3);
                int code = parts.Length > 1 && int.TryParse(parts[1], out int c) ? c : 0;
                string text = parts.Length > 2 ? parts[2] : "error";
                _pending.TrySetResult(new Reply(false, code, text, _data.ToArray()));
            }
        }
    }

    private void FailPending(string reason)
    {
        lock (_sync)
        {
            _pending?.TrySetException(new IOException(reason));
        }
    }

    /// <summary>Sends a command and waits for OK / ERR.</summary>
    public async Task<Reply> SendAsync(string command, int timeoutMs = 2000, CancellationToken ct = default)
    {
        await _commandLock.WaitAsync(ct).ConfigureAwait(false);
        try
        {
            var port = _port ?? throw new IOException(Loc.T("X.NoConnection"));
            var tcs = new TaskCompletionSource<Reply>(TaskCreationOptions.RunContinuationsAsynchronously);
            lock (_sync)
            {
                _data.Clear();
                _pending = tcs;
            }

            LineLogged?.Invoke(command, true);
            try
            {
                port.Write(command + "\n");
            }
            catch (Exception ex) when (ex is InvalidOperationException or TimeoutException)
            {
                throw new IOException(Loc.F("X.WriteError", ex.Message), ex);
            }

            try
            {
                return await tcs.Task.WaitAsync(TimeSpan.FromMilliseconds(timeoutMs), ct).ConfigureAwait(false);
            }
            catch (TimeoutException)
            {
                throw new TimeoutException(Loc.F("X.NoReply", command));
            }
            finally
            {
                lock (_sync)
                {
                    if (_pending == tcs) _pending = null;
                }
            }
        }
        finally
        {
            _commandLock.Release();
        }
    }

    /// <summary>Sends a command, throws <see cref="StationException"/> on ERR, returns the data lines.</summary>
    public async Task<IReadOnlyList<string>> QueryAsync(string command, int timeoutMs = 2000)
    {
        var reply = await SendAsync(command, timeoutMs).ConfigureAwait(false);
        if (!reply.Ok) throw new StationException(reply.ErrorCode, reply.ErrorText);
        return reply.Data;
    }
}
