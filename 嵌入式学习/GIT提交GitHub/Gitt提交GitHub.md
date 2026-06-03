## **方法流程**
1.初始化Git
	git init
2.配置提交身份
	 git config --global user.name "*用户名*"
	 git config --global user.email "*邮箱*"
3.添加文件到配置区
	 git add .
4.提交到本地仓库
	 git commit -m "*首次提交*"
> 5.关联远程仓库
> 	 git remote add origin htps:
> 6.处理分支名差异
> 	 main 替=替换成master
> 7.推送代码
> 	 git push -u origin master
>全部按照GitHub上面的推动代码直接复制即可

## **常用检查命令**
1. 查看远程仓库地址 git remote -v
2. 查看当前分支及跟踪关系 git branch -vv
3. 查看全局配置 git config --global --list
## 后续日常更新流程
git add.%%*添加所有改动*%%
git commit -m "本次更新说明"%%*提交到本地*%%
git push %%*推送到GitHub*%%


