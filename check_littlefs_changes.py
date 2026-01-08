#!/usr/bin/env python3
"""
LittleFS文件系统变化检测脚本
用于检测littlefs目录内容是否发生变化，避免不必要的文件系统镜像重新生成
"""

import os
import hashlib
import json
import sys
from pathlib import Path

def calculate_directory_hash(directory_path):
    """计算目录内容的哈希值"""
    hash_object = hashlib.md5()
    
    # 遍历目录中的所有文件
    for root, dirs, files in os.walk(directory_path):
        # 按字母顺序排序以确保一致性
        dirs.sort()
        files.sort()
        
        for file in files:
            file_path = os.path.join(root, file)
            # 计算文件相对路径的哈希
            rel_path = os.path.relpath(file_path, directory_path)
            hash_object.update(rel_path.encode('utf-8'))
            
            # 计算文件内容的哈希
            try:
                with open(file_path, 'rb') as f:
                    while chunk := f.read(8192):
                        hash_object.update(chunk)
            except Exception as e:
                print(f"警告: 无法读取文件 {file_path}: {e}")
    
    return hash_object.hexdigest()

def save_hash_to_file(hash_value, hash_file_path):
    """保存哈希值到文件"""
    if not hash_file_path:
        print("错误: 哈希文件路径为空")
        return False
        
    try:
        # 确保目录存在
        os.makedirs(os.path.dirname(hash_file_path), exist_ok=True)
        with open(hash_file_path, 'w') as f:
            json.dump({'hash': hash_value}, f)
        return True
    except Exception as e:
        print(f"错误: 无法保存哈希文件: {e}")
        return False

def load_hash_from_file(hash_file_path):
    """从文件加载哈希值"""
    try:
        with open(hash_file_path, 'r') as f:
            data = json.load(f)
            return data.get('hash')
    except (FileNotFoundError, json.JSONDecodeError):
        return None

def main():
    if len(sys.argv) != 3:
        print("用法: python check_littlefs_changes.py <littlefs目录> <哈希文件路径>")
        sys.exit(1)
    
    littlefs_dir = sys.argv[1]
    hash_file = sys.argv[2]
    
    # 检查目录是否存在
    if not os.path.exists(littlefs_dir):
        print(f"错误: 目录不存在: {littlefs_dir}")
        sys.exit(1)
    
    # 计算当前目录哈希
    current_hash = calculate_directory_hash(littlefs_dir)
    print(f"当前目录哈希: {current_hash}")
    
    # 加载之前的哈希值
    previous_hash = load_hash_from_file(hash_file)
    
    if previous_hash is None:
        print("未找到之前的哈希值，首次运行")
        # 保存当前哈希值
        if save_hash_to_file(current_hash, hash_file):
            print("已保存新的哈希值")
            # 返回1表示需要重新生成镜像
            sys.exit(1)
        else:
            print("无法保存哈希值")
            sys.exit(2)
    
    print(f"之前的哈希值: {previous_hash}")
    
    # 比较哈希值
    if current_hash == previous_hash:
        print("文件系统内容未发生变化")
        # 返回0表示不需要重新生成镜像
        sys.exit(0)
    else:
        print("文件系统内容已发生变化")
        # 保存新的哈希值
        if save_hash_to_file(current_hash, hash_file):
            print("已更新哈希值")
            # 返回1表示需要重新生成镜像
            sys.exit(1)
        else:
            print("无法更新哈希值")
            sys.exit(2)

if __name__ == "__main__":
    main()