package com.droiddeck.launcher.gpu;

import android.content.Context;
import android.os.Process;
import android.util.Log;

import com.droiddeck.launcher.core.HostProcess;
import com.droiddeck.launcher.core.SessionPart;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;

/**
 * The Venus server: the Linux runtime's Vulkan on a GPU Turnip cannot drive (Mali on Google
 * Tensor).
 *
 * <p>Nothing in the runtime can reach a Mali directly - the vendor's driver is a bionic library and
 * the runtime's programs are glibc. So the runtime's Vulkan loader is pointed at Mesa's Venus driver
 * (staged at {@link #GUEST_DIR}), which serializes every Vulkan call over a unix socket, and this
 * server - virglrenderer's vtest server, built for Android by tools/venus/build-host.sh - executes
 * them out here on the device's own Vulkan driver. Memory the guest maps is allocated as
 * AHardwareBuffers, whose dma-bufs both sides can map (tools/venus/patches).
 *
 * <p>The socket sits under the app's files directory, which the session binds at its own path, so
 * the guest connects to the very path the server listens on. The server forks a child per
 * connection and starts the render server ({@link #RENDER_SERVER}) for the Vulkan contexts.
 */
public class VenusServer extends SessionPart {
    private static final String TAG = "VenusServer";
    /** The vtest server and the render server, named as libraries so Android will run them. */
    public static final String SERVER = "libvirgl_test_server.so";
    public static final String RENDER_SERVER = "libvirgl_render_server.so";
    /** Where the runtime's Venus driver and its manifest are staged (SessionFiles). */
    public static final String GUEST_DIR = "usr/local/lib/droiddeck-venus";
    public static final String GUEST_ICD = GUEST_DIR + "/virtio_icd.json";

    private final File socketPath;
    private final File logFile;
    private volatile int pid = -1;

    public VenusServer(File socketPath, File logFile) {
        this.socketPath = socketPath;
        this.logFile = logFile;
    }

    /** Both halves are there: the server in the apk, and the driver staged in the runtime. */
    public static boolean available(Context context, File runtimeRoot) {
        File libDir = new File(context.getApplicationInfo().nativeLibraryDir);
        return new File(libDir, SERVER).isFile() && new File(libDir, RENDER_SERVER).isFile()
                && new File(runtimeRoot, GUEST_ICD).isFile()
                && new File(runtimeRoot, GUEST_DIR + "/libvulkan_virtio.so").isFile();
    }

    public File socket() {
        return socketPath;
    }

    @Override
    public void start() {
        stop();
        File libDir = new File(app().getApplicationInfo().nativeLibraryDir);
        File server = new File(libDir, SERVER);
        if (!server.isFile()) {
            Log.w(TAG, "server missing at " + server);
            return;
        }
        File parent = socketPath.getParentFile();
        if (parent != null) {
            //noinspection ResultOfMethodCallIgnored
            parent.mkdirs();
        }
        // A socket left by a session that did not shut down cleanly would make the bind fail.
        //noinspection ResultOfMethodCallIgnored
        socketPath.delete();

        String command = server.getAbsolutePath() + " --venus --no-virgl --socket-path " + socketPath.getAbsolutePath();
        ArrayList<String> env = new ArrayList<>();
        env.add("HOME=" + app().getFilesDir());
        env.add("TMPDIR=" + app().getCacheDir());
        env.add("RENDER_SERVER_EXEC_PATH=" + new File(libDir, RENDER_SERVER).getAbsolutePath());
        PrintWriter out = null;
        if (logFile != null) {
            try {
                out = new PrintWriter(new FileWriter(logFile, true));
                out.println("== Venus server starting on " + socketPath);
                out.flush();
            } catch (Exception e) {
                Log.w(TAG, "could not open " + logFile, e);
            }
        }
        final PrintWriter log = out;
        pid = HostProcess.start(command, env.toArray(new String[0]), app().getFilesDir(), null,
                line -> {
                    Log.i(TAG, line);
                    if (log != null) synchronized (log) { log.println(line); log.flush(); }
                });
        // The guest's first Vulkan instance connects as soon as gamescope starts: give the server
        // the moment it needs to listen, so the first connection does not find no socket at all.
        long deadline = System.currentTimeMillis() + 2000L;
        while (pid != -1 && !socketPath.exists() && System.currentTimeMillis() < deadline) {
            try {
                Thread.sleep(20L);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }
        }
        Log.i(TAG, "started pid=" + pid + " socket=" + socketPath + (socketPath.exists() ? "" : " (not listening yet)"));
    }

    @Override
    public void stop() {
        if (pid != -1) {
            // The server's per-connection children and the render server are in its process group
            // only if it made one; killing it closes the listening socket, and each child exits
            // when its guest connection does.
            Process.killProcess(pid);
            pid = -1;
        }
    }

    @Override
    public int suspendPid() {
        return pid;
    }
}
