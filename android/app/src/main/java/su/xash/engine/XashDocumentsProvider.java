package su.xash.engine;

import android.annotation.TargetApi;
import android.content.res.AssetFileDescriptor;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.graphics.Point;
import android.os.Build;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract.Document;
import android.provider.DocumentsContract.Root;
import android.provider.DocumentsProvider;
import android.webkit.MimeTypeMap;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;

@TargetApi(Build.VERSION_CODES.KITKAT)
public class XashDocumentsProvider extends DocumentsProvider {
    private static final String ALL_MIME_TYPES = "*/*";
    private File mRootDir;

    private static final String TAG = "XashDocumentsProvider";

    @Override
    public boolean onCreate() {
        mRootDir = getContext().getExternalFilesDir(null);

        return true;
    }

    private static final String[] DEFAULT_ROOT_PROJECTION = new String[]{Root.COLUMN_ROOT_ID,
            Root.COLUMN_MIME_TYPES,
            Root.COLUMN_FLAGS,
            Root.COLUMN_ICON,
            Root.COLUMN_TITLE,
            Root.COLUMN_SUMMARY,
            Root.COLUMN_DOCUMENT_ID,
            Root.COLUMN_AVAILABLE_BYTES};

    private static final String[] DEFAULT_DOCUMENT_PROJECTION = new String[]{Document.COLUMN_DOCUMENT_ID,
            Document.COLUMN_MIME_TYPE,
            Document.COLUMN_DISPLAY_NAME,
            Document.COLUMN_LAST_MODIFIED,
            Document.COLUMN_FLAGS,
            Document.COLUMN_SIZE};

    @Override
    public Cursor queryRoots(String[] projection) {
        final MatrixCursor result = new MatrixCursor(projection != null ? projection : DEFAULT_ROOT_PROJECTION);

        final String appName = getContext().getString(R.string.app_name);
        final String docId = getDocIdForFile(mRootDir);

        int flags;

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
            flags = Root.FLAG_LOCAL_ONLY | Root.FLAG_SUPPORTS_CREATE | Root.FLAG_SUPPORTS_SEARCH;
        } else {
            flags = Root.FLAG_LOCAL_ONLY | Root.FLAG_SUPPORTS_CREATE | Root.FLAG_SUPPORTS_SEARCH | Root.FLAG_SUPPORTS_IS_CHILD;
        }

        final MatrixCursor.RowBuilder row = result.newRow();
        row.add(Root.COLUMN_ROOT_ID, docId);
        row.add(Root.COLUMN_DOCUMENT_ID, docId);
        row.add(Root.COLUMN_SUMMARY, null);
        row.add(Root.COLUMN_FLAGS, flags);
        row.add(Root.COLUMN_TITLE, appName);
        row.add(Root.COLUMN_MIME_TYPES, ALL_MIME_TYPES);
        row.add(Root.COLUMN_AVAILABLE_BYTES, mRootDir.getFreeSpace());
        row.add(Root.COLUMN_ICON, R.mipmap.ic_launcher);

        return result;
    }

    @Override
    public Cursor queryDocument(String documentId, String[] projection) throws FileNotFoundException {
        final MatrixCursor result = new MatrixCursor(projection != null ? projection : DEFAULT_DOCUMENT_PROJECTION);

        includeFile(result, documentId, null);

        return result;
    }

    @Override
    public Cursor queryChildDocuments(String parentDocumentId, String[] projection, String sortOrder) throws FileNotFoundException {
        final MatrixCursor result = new MatrixCursor(projection != null ? projection : DEFAULT_DOCUMENT_PROJECTION);

        final File parent = getFileForDocId(parentDocumentId);
        final File[] filesList = parent.listFiles();

        if (filesList != null) {
            for (File file : filesList) {
                includeFile(result, null, file);
            }
        }

        return result;
    }

    @Override
    public ParcelFileDescriptor openDocument(final String documentId, String mode, CancellationSignal signal) throws FileNotFoundException {
        final File file = getFileForDocId(documentId);

        final int accessMode = ParcelFileDescriptor.parseMode(mode);

        return ParcelFileDescriptor.open(file, accessMode);
    }

    @Override
    public AssetFileDescriptor openDocumentThumbnail(String documentId, Point sizeHint, CancellationSignal signal) throws FileNotFoundException {
        final File file = getFileForDocId(documentId);

        final ParcelFileDescriptor pfd = ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);

        return new AssetFileDescriptor(pfd, 0, file.length());
    }

    @Override
    public String createDocument(String parentDocumentId, String mimeType, String displayName) throws FileNotFoundException {
        File newFile = new File(parentDocumentId, displayName);

        int noConflictId = 1;

        while (newFile.exists()) {
            newFile = new File(parentDocumentId, displayName + " (" + noConflictId++ + ")");
        }

        try {
            boolean succeeded;

            if (Document.MIME_TYPE_DIR.equals(mimeType)) {
                succeeded = newFile.mkdir();
            } else {
                succeeded = newFile.createNewFile();
            }

            if (!succeeded) {
                throw new FileNotFoundException("Failed to create document with id " + newFile.getPath());
            }
        } catch (IOException e) {
            throw new FileNotFoundException("Failed to create document with id " + newFile.getPath());
        }

        return newFile.getPath();
    }

    @Override
    public void deleteDocument(String documentId) throws FileNotFoundException {
        File file = getFileForDocId(documentId);

        if (file.isDirectory()) {
            if (!deleteDirectory(file)) {
                throw new FileNotFoundException("Failed to delete document with id " + documentId);
            }
        } else if (!file.delete()) {
            throw new FileNotFoundException("Failed to delete document with id " + documentId);
        }
    }

    @Override
    public String getDocumentType(String documentId) throws FileNotFoundException {
        File file = getFileForDocId(documentId);

        return getMimeType(file);
    }

    @Override
    public boolean isChildDocument(String parentDocumentId, String documentId) {
        return documentId.startsWith(parentDocumentId);
    }

    private static File getFileForDocId(String docId) throws FileNotFoundException {
        final File f = new File(docId);

        if (!f.exists()) throw new FileNotFoundException(f.getAbsolutePath() + " not found");

        return f;
    }

    private static String getDocIdForFile(File file) {
        return file.getAbsolutePath();
    }

    private static String getMimeType(File file) {
        if (file.isDirectory()) {
            return Document.MIME_TYPE_DIR;
        } else {
            final String name = file.getName();
            final int lastDot = name.lastIndexOf('.');

            if (lastDot >= 0) {
                final String extension = name.substring(lastDot + 1).toLowerCase();
                final String mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension);

                if (mime != null) return mime;
            }

            return "application/octet-stream";
        }
    }

    private static boolean deleteDirectory(File dir) {
        final File[] allContents = dir.listFiles();

        if (allContents != null) {
            for (File file : allContents) {
                deleteDirectory(file);
            }
        }
        return dir.delete();
    }

    private void includeFile(MatrixCursor result, String docId, File file) throws FileNotFoundException {
        if (docId == null) {
            docId = getDocIdForFile(file);
        } else {
            file = getFileForDocId(docId);
        }

        int flags = 0;

        if (file.isDirectory()) {
            if (file.canWrite()) {
                flags |= Document.FLAG_DIR_SUPPORTS_CREATE;
            }
        } else if (file.canWrite()) {
            flags |= Document.FLAG_SUPPORTS_WRITE;
        }

        File parentFile = file.getParentFile();
        if (parentFile != null && parentFile.canWrite()) {
            flags |= Document.FLAG_SUPPORTS_DELETE;
        }

        final String displayName = file.getName();
        final String mimeType = getMimeType(file);

        if (mimeType.startsWith("image/")) {
            flags |= Document.FLAG_SUPPORTS_THUMBNAIL;
        }

        final MatrixCursor.RowBuilder row = result.newRow();
        row.add(Document.COLUMN_DOCUMENT_ID, docId);
        row.add(Document.COLUMN_DISPLAY_NAME, displayName);
        row.add(Document.COLUMN_SIZE, file.length());
        row.add(Document.COLUMN_MIME_TYPE, mimeType);
        row.add(Document.COLUMN_LAST_MODIFIED, file.lastModified());
        row.add(Document.COLUMN_FLAGS, flags);
        row.add(Document.COLUMN_ICON, R.mipmap.ic_launcher);
    }
}
