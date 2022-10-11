static const char webui_html[] = {
"<!DOCTYPE html>"
"<html>"
"<head>"
"<title>Retro-Go Webui</title>"
"<style>body{width:600px;margin:0 auto;font-family:monospace;}.disabled{pointer-events:none;opacity: 0.7;background:#DDD;}</style>"
"</head>"
"<body>"
"    <h1>Retro-Go Web Interface</h1>"
"    <h3 id='subtitle'></h3>"
"    <div>"
"        <table id='filebrowser'>"
"            <thead><tr><th style='width:400px'>Filename</th><th style='width:60px'>Size</th><th>Action</th></tr></thead>"
"            <tbody></tbody>"
"        </table>"
"        <hr>"
"        <button onclick='update_view(current_path)'>Refresh</button>"
"        | <button onclick='create_folder()'>Create folder</button>"
"        | <button onclick='create_file()'>Create file</button>"
"        | Upload: <input type='file' id='upload' onchange='upload_files()' multiple><label id='status'></label>"
"    </div>"
"    <script>"
"        let current_path = '';"
"        function $(selector) {"
"            return document.querySelector(selector);"
"        }"
"        function basename(path) {"
"            return path.match('(.*)\\/(.*)')[2];"
"        }"
"        function dirname(path) {"
"            return path.match('(.*)\\/(.*)')[1];"
"        }"
"        function api_req(cmd, arg1, arg2, callback, type) {"
"            var xhr = new XMLHttpRequest();"
"            xhr.responseType = type || 'json';"
"            xhr.addEventListener('loadstart', function() {"
"                $('#filebrowser').classList.add('disabled');"
"            });"
"            xhr.addEventListener('loadend', function() {"
"                $('#filebrowser').classList.remove('disabled');"
"            });"
"            xhr.addEventListener('load', callback);"
"            xhr.open('POST', '/api');"
"            xhr.send(JSON.stringify({ cmd, arg1, arg2 }));"
"        }"
"        function delete_file(path) {"
"            if (confirm('Delete ' + path + ' ?')) {"
"                api_req('delete', path, null, function () {"
"                    update_view(dirname(path));"
"                });"
"            }"
"        }"
"        function rename_file(path) {"
"            let new_path = prompt('New name for ' + path, path);"
"            if (new_path) {"
"                api_req('rename', path, new_path, function () {"
"                    update_view(dirname(path));"
"                });"
"            }"
"        }"
"        function create_file() {"
"            let new_file = prompt('New file name', 'new file');"
"            if (new_file) {"
"                api_req('touch', current_path + '/' + new_file, '', function () {"
"                    update_view(current_path);"
"                });"
"            }"
"        }"
"        function create_folder() {"
"            let new_folder = prompt('New folder name', 'new folder');"
"            if (new_folder) {"
"                api_req('mkdir', current_path + '/' + new_folder, '', function () {"
"                    update_view(current_path);"
"                });"
"            }"
"        }"
"        function download_file(path) {"
"            window.open(path, '_blank').focus();"
"        }"
"        function upload_files() {"
"            let files = $('#upload').files;"
"            for (let file of files) {"
"                var xhr = new XMLHttpRequest();"
"                xhr.addEventListener('loadstart', function() {"
"                    $('#filebrowser').classList.add('disabled');"
"                });"
"                xhr.addEventListener('load', function () {"
"                    update_view(current_path);"
"                });"
"                xhr.upload.addEventListener('progress', function (e) {"
"                    $('#status').innerText = Math.floor(e.loaded/e.total * 100) + '%';"
"                });"
"                xhr.open('PUT', current_path + '/' + file.name);"
"                xhr.send(file);"
"            }"
"            $('#upload').value = '';"
"        }"
"        function update_view(path) {"
"            let btn = function (lbl, fn, arg) {"
"                return '<a href=\"#\" onclick=\"' + fn + '(\\'' + arg.replace(/'/g, '\\\\\\'') +  '\\')\">' + lbl + '</a>';"
"            };"
"            api_req('list', path, '', function () {"
"                let html = '<tr><td>' + btn('..', 'update_view', dirname(path)) + '</td></tr>';"
"                let files = this.response.files;"
"                files.sort((a, b) => (a.name < b.name ? -1 : (a.name > b.name ? 1 : 0)));"
"                files.sort((a, b) => (a.is_dir > b.is_dir ? -1 : (a.is_dir < b.is_dir ? 1 : 0)));"
"                for (let f of files) {"
"                    f.path = path + '/' + f.name;"
"                    html += '<tr>';"
"                    if (f.is_dir) {"
"                        html += '<td>' + btn(f.name + '/', 'update_view', f.path) + '</td>';"
"                    } else {"
"                        html += '<td>' + btn(f.name, 'download_file', f.path) + '</td>';"
"                    }"
"                    html += '<td>' + f.size + '</td>';"
"                    html += '<td>' + btn('Rename', 'rename_file', f.path) + ' | ' + btn('Delete', 'delete_file', f.path) + '</td>';"
"                    html += '</tr>';"
"                }"
"                $('#filebrowser tbody').innerHTML = html;"
"                $('#subtitle').innerText = path;"
"                $('#status').innerText = '';"
"                current_path = path;"
"            });"
"        }"
"        update_view('/sd');"
"    </script>"
"</body>"
"</html>"
};
