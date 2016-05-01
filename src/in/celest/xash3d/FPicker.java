package in.celest.xash3d;
//Created by Solexid
import android.app.Activity;
import android.app.ListActivity;
import android.content.Context;
import android.content.Intent;
import android.graphics.drawable.Drawable;
import android.os.Environment;
import android.os.Bundle;
import android.os.Build;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.ImageView;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.text.DateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.List;

import in.celest.xash3d.hl.R;

public class FPicker extends Activity {
    private File currentDir;
    private FileArrayAdapter adapter;
    static ListView delta;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if ( Build.VERSION.SDK_INT >= 21 )
            super.setTheme( 0x01030224 );
        setContentView(R.layout.activity_fpicker);
        String path = Environment.getExternalStorageDirectory().toString();
        currentDir = new File(path);

        fill(currentDir);
    }

    private void fill(File folder)
    {
        File[]dirs = folder.listFiles();
        this.setTitle("Current Dir: "+folder.getName());
        List<Item> dir = new ArrayList<Item>();

            for(File ff: dirs)
            {
                Date lastModDate = new Date(ff.lastModified());
                DateFormat formater = DateFormat.getDateTimeInstance();
                String date_modify = formater.format(lastModDate);
                if(ff.isDirectory()){
                boolean isXashDir=false;

                    File[] fbuf = ff.listFiles();
                    int buf = 0;
                    if(fbuf != null&&fbuf.length<20){
                        buf = fbuf.length;
                        for (File valves: fbuf) {
                            if (valves.isDirectory()) {
                                if(valves.getName().contains("valve"))isXashDir=true;
                            }
                        }
                    }
                    String num_item = String.valueOf(buf);
                    if(buf == 0) num_item = "Some items";
                    else num_item +=" items";
                    if(isXashDir){
                         dir.add(new Item(ff.getName(),num_item,date_modify,ff.getAbsolutePath(),R.drawable.ic_launcher));

                    }else {
                        dir.add(new Item(ff.getName(), num_item, date_modify, ff.getAbsolutePath(), R.drawable.folder));
                    }}
            }

        Collections.sort(dir);
        if(folder.getName().length()>1)
            dir.add(0,new Item("..","Parent Directory","",folder.getParent(),R.drawable.folder));
        adapter = new FileArrayAdapter(FPicker.this,R.layout.row,dir);
        delta = (ListView)findViewById(R.id.FileView);
        delta.setAdapter(adapter);
        delta.setOnItemClickListener(new AdapterView.OnItemClickListener( ){
            @Override
            public void onItemClick(AdapterView<?> parent , View v, int position, long id) {
            Item o = adapter.getItem(position);
             currentDir = new File(o.getPath());
             fill(currentDir);

        }
        });
    }
    public void onFileClick(View v)
    {
        Toast.makeText(this, "Chosen path : " + currentDir, Toast.LENGTH_SHORT).show();
        Intent intent = new Intent();
        intent.putExtra("GetPath",currentDir.toString());
        setResult(RESULT_OK, intent);
        finish();
    }


}
class FileArrayAdapter extends ArrayAdapter<Item> {

    private Context c;
    private int id;
    private List<Item>items;

    public FileArrayAdapter(Context context, int textViewResourceId,
                            List<Item> objects) {
        super(context, textViewResourceId, objects);
        c = context;
        id = textViewResourceId;
        items = objects;
    }
    public Item getItem(int i)
    {
        return items.get(i);
    }
    @Override
    public View getView(int position, View convertView, ViewGroup parent) {
        View v = convertView;
        if (v == null) {
            LayoutInflater vi = (LayoutInflater)c.getSystemService(Context.LAYOUT_INFLATER_SERVICE);
            v = vi.inflate(id, null);
        }


        final Item finstance = items.get(position);
        if (finstance != null) {
            TextView filename = (TextView) v.findViewById(R.id.filename);
            TextView fileitems = (TextView) v.findViewById(R.id.fileitems);
            TextView filedate = (TextView) v.findViewById(R.id.filedate);
            ImageView imageicon = (ImageView) v.findViewById(R.id.fd_Icon1);


            Drawable image = c.getResources().getDrawable(finstance.getImage());
            imageicon.setImageDrawable(image);

            if(filename!=null)
                filename.setText(finstance.getName());
            if(fileitems!=null)
                fileitems.setText(finstance.getData());
            if(filedate!=null)
                filedate.setText(finstance.getDate());
        }
        return v;
    }
}
class Item implements Comparable<Item>{
    private String name;
    private String data;
    private String date;
    private String path;
    private int image;

    public Item(String n,String d, String dt, String p, int img)
    {
        name = n;
        data = d;
        date = dt;
        path = p;
        image = img;
    }
    public String getName()
    {
        return name;
    }
    public String getData()
    {
        return data;
    }
    public String getDate()
    {
        return date;
    }
    public String getPath()
    {
        return path;
    }
    public int getImage() {
        return image;
    }
    public int compareTo(Item o) {
        if(this.name != null)
            return this.name.toLowerCase().compareTo(o.getName().toLowerCase());
        else
            throw new IllegalArgumentException();
    }
}
